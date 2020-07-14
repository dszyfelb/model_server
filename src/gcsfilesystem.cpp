// Copyright (c) 2019-2020, NVIDIA CORPORATION. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#include "gcsfilesystem.hpp"

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include <spdlog/spdlog.h>

namespace ovms {

namespace fs = std::filesystem;
namespace gcs = google::cloud::storage;

StatusCode GCSFileSystem::parsePath(const std::string& path,
    std::string* bucket, std::string* object) {
    int bucket_start = path.find("gs://") + strlen("gs://");
    int bucket_end = path.find("/", bucket_start);
    if (bucket_end > bucket_start) {
        *bucket = path.substr(bucket_start, bucket_end - bucket_start);
        *object = path.substr(bucket_end + 1);
    } else {
        *bucket = path.substr(bucket_start);
        *object = "";
    }
    if (bucket->empty()) {
        return StatusCode::GCS_BUCKET_NOT_FOUND;
    }
    return StatusCode::OK;
}

namespace {

google::cloud::storage::ClientOptions createDefaultOrAnonymousClientOptions() {
    if (std::getenv("GOOGLE_APPLICATION_CREDENTIALS") == nullptr) {
        auto credentials =
            google::cloud::storage::v1::oauth2::CreateAnonymousCredentials();
        if (!credentials) {
            SPDLOG_ERROR("Unable to create default GCS credentials");
            throw std::runtime_error("Unable to create default GCS credentials");
        }
        auto options = google::cloud::storage::ClientOptions(credentials);
        return options;
    } else {
        auto credentials =
            google::cloud::storage::v1::oauth2::GoogleDefaultCredentials();
        if (!credentials) {
            SPDLOG_ERROR("Unable to create default GCS credentials");
            throw std::runtime_error("Unable to create default GCS credentials");
        }
        auto options = google::cloud::storage::ClientOptions(*credentials);
        return options;
    }
}

}  // namespace

GCSFileSystem::GCSFileSystem() :
    client_{createDefaultOrAnonymousClientOptions()} {
    SPDLOG_TRACE("GCSFileSystem default ctor");
}

GCSFileSystem::GCSFileSystem(const gcs::v1::ClientOptions& options) :
    client_{options, gcs::StrictIdempotencyPolicy()} {
    SPDLOG_TRACE("GCSFileSystem ctor with custom options");
}

GCSFileSystem::~GCSFileSystem() { SPDLOG_TRACE("GCSFileSystem dtor"); }

StatusCode GCSFileSystem::fileExists(const std::string& path, bool* exists) {
    *exists = false;
    std::string bucket, object;

    auto status = this->parsePath(path, &bucket, &object);
    if (status != StatusCode::OK) {
        SPDLOG_WARN("GCS: Unable to parse path: {} -> {}", path,
            ovms::Status(status).string());
        return status;
    }

    google::cloud::StatusOr<gcs::ObjectMetadata> object_metadata =
        client_.GetObjectMetadata(bucket, object);
    if (object_metadata) {
        *exists = true;
        return StatusCode::OK;
    }
    bool is_directory;
    auto dir_status = this->isDirectory(path, &is_directory);
    if (dir_status != StatusCode::OK) {
        SPDLOG_WARN("GCS: isDirectory failed: {} -> {}", path,
            ovms::Status(status).string());
        return dir_status;
    }
    *exists = is_directory;
    SPDLOG_TRACE("GCS: fileExits {} -> {}", path, is_directory);
    return StatusCode::OK;
}

namespace {

std::string AppendSlash(const std::string& name) {
    if (name.empty() || (name.back() == '/')) {
        return name;
    }

    return (name + "/");
}

bool isAbsolutePath(const std::string& path) {
    return !path.empty() && (path[0] == '/');
}

std::string joinPath(std::initializer_list<std::string> segments) {
    std::string joined;

    for (const auto& seg : segments) {
        if (joined.empty()) {
            joined = seg;
        } else if (isAbsolutePath(seg)) {
            if (joined[joined.size() - 1] == '/') {
                joined.append(seg.substr(1));
            } else {
                joined.append(seg);
            }
        } else {
            if (joined[joined.size() - 1] != '/') {
                joined.append("/");
            }
            joined.append(seg);
        }
    }

    return joined;
}

StatusCode CreateLocalDir(const std::string& path) {
    int status =
        mkdir(const_cast<char*>(path.c_str()), S_IRUSR | S_IWUSR | S_IXUSR);
    if (status == -1) {
        SPDLOG_ERROR("Failed to create local folder: {} {} ", path,
            strerror(errno));
        return StatusCode::PATH_INVALID;
    }
    return StatusCode::OK;
}

}  // namespace

StatusCode GCSFileSystem::isDirectory(const std::string& path,
    bool* is_directory) {
    *is_directory = false;
    std::string bucket, object;
    auto status = this->parsePath(path, &bucket, &object);
    if (status != StatusCode::OK) {
        SPDLOG_WARN("GCS: Unable to parse path: {} -> {}", path,
            ovms::Status(status).string());
        return status;
    }
    if (path.empty()) {
        SPDLOG_TRACE("GCS: path is empty -> always a directory");
        *is_directory = true;
        return StatusCode::OK;
    }
    for (auto&& meta :
        client_.ListObjects(bucket, gcs::Prefix(AppendSlash(object)))) {
        if (meta) {
            *is_directory = true;
            break;
        }
    }
    return StatusCode::OK;
}

StatusCode GCSFileSystem::fileModificationTime(const std::string& path,
    int64_t* mtime_ns) {
    SPDLOG_TRACE("fileModificationTime {}", path);
    bool is_directory;
    auto status = this->isDirectory(path, &is_directory);
    if (status != StatusCode::OK) {
        SPDLOG_WARN("GCS: Unable to get file modification time {} -> {}", path,
            ovms::Status(status).string());
        return status;
    }
    if (is_directory) {
        SPDLOG_WARN("GCS: fileModificationTime called for a directory: {}", path);
        return StatusCode::GCS_INCORRECT_REQUESTED_OBJECT_TYPE;
    }
    std::string bucket, object;
    status = this->parsePath(path, &bucket, &object);
    if (status != StatusCode::OK) {
        SPDLOG_WARN("GCS: Unable to get file modification time {} -> {}", path,
            ovms::Status(status).string());
        return status;
    }
    google::cloud::StatusOr<gcs::ObjectMetadata> meta =
        client_.GetObjectMetadata(bucket, object);
    if (!meta) {
        SPDLOG_WARN("GCS: Unable to get file modification time {} -> object "
                    "metadata is null",
            path);
        return StatusCode::GCS_METADATA_FAIL;
    }
    auto update_time =
        std::chrono::time_point_cast<std::chrono::nanoseconds>(meta->updated())
            .time_since_epoch()
            .count();
    *mtime_ns = update_time;
    SPDLOG_TRACE("GCS: modification time for {} is {}", path, update_time);
    return StatusCode::OK;
}

StatusCode
GCSFileSystem::getDirectoryContents(const std::string& path,
    std::set<std::string>* contents) {
    SPDLOG_TRACE("GCS: getting directory contents {}", path);
    std::string bucket, directory_path, full_directory;
    auto status = this->parsePath(path, &bucket, &directory_path);
    if (status != StatusCode::OK) {
        SPDLOG_WARN("GCS: Unable to get directory content {} -> {}", path,
            ovms::Status(status).string());
        return status;
    }
    full_directory = AppendSlash(directory_path);
    for (auto&& meta : client_.ListObjects(bucket, gcs::Prefix(full_directory))) {
        if (!meta) {
            SPDLOG_WARN("GCS: Unable to get directory content {} -> object metadata "
                        "is empty.");
            return StatusCode::GCS_INVALID_ACCESS;
        }
        // ignore self:
        if (meta->name() == full_directory) {
            continue;
        }

        // keep only basename:
        std::string name = meta->name();
        int name_start = name.find(full_directory) + full_directory.size();
        int name_end = name.find("/", name_start);
        contents->insert(name.substr(name_start, name_end - name_start));
    }
    SPDLOG_TRACE("GCS: directory contents fetched, items: {}", contents->size());
    return StatusCode::OK;
}

StatusCode GCSFileSystem::getDirectorySubdirs(const std::string& path,
    std::set<std::string>* subdirs) {
    SPDLOG_TRACE("GCS: listing directory subdirs: {}", path);
    auto status = this->getDirectoryContents(path, subdirs);
    if (status != StatusCode::OK) {
        SPDLOG_WARN("GCS: Unable to list directory subdir content {} -> {}", path,
            ovms::Status(status).string());
        return status;
    }
    for (auto item = subdirs->begin(); item != subdirs->end();) {
        bool is_directory;
        auto status = this->isDirectory(joinPath({path, *item}), &is_directory);
        if (status != StatusCode::OK) {
            SPDLOG_WARN("GCS: Unable to list directory subdir content {} -> {}", path,
                ovms::Status(status).string());
            return status;
        }
        if (!is_directory) {
            item = subdirs->erase(item);
        } else {
            ++item;
        }
    }
    SPDLOG_TRACE("GCS: listing directory subdirs ok: {}", path);
    return StatusCode::OK;
}

StatusCode GCSFileSystem::getDirectoryFiles(const std::string& path,
    std::set<std::string>* files) {
    SPDLOG_TRACE("GCS: listing directory: {}", path);
    auto status = this->getDirectoryContents(path, files);
    if (status != StatusCode::OK) {
        SPDLOG_WARN("GCS: Unable to list directory content {} -> {}", path,
            ovms::Status(status).string());
        return status;
    }
    for (auto item = files->begin(); item != files->end();) {
        bool is_directory;
        auto status = this->isDirectory(joinPath({path, *item}), &is_directory);
        if (status != StatusCode::OK) {
            SPDLOG_WARN("GCS: Unable to list directory content {} -> {}", path,
                ovms::Status(status).string());
            return status;
        }
        if (is_directory) {
            item = files->erase(item);
        } else {
            ++item;
        }
    }
    SPDLOG_TRACE("GCS: listing directory ok for {}", path);
    return StatusCode::OK;
}

StatusCode GCSFileSystem::readTextFile(const std::string& path,
    std::string* contents) {
    SPDLOG_TRACE("GCS: Downloading file {}", path);
    bool exists;
    auto status = fileExists(path, &exists);
    if (status != StatusCode::OK) {
        return status;
    }
    if (!exists) {
        SPDLOG_WARN("GCS: Downloading file -> file does not exist at {}", path);
        return StatusCode::GCS_FILE_NOT_FOUND;
    }
    std::string bucket, object;
    status = parsePath(path, &bucket, &object);
    if (status != StatusCode::OK) {
        return status;
    }
    gcs::ObjectReadStream stream = client_.ReadObject(bucket, object);
    if (!stream) {
        SPDLOG_WARN("GCS: Downloading file has failed: ", path);
        return StatusCode::GCS_FILE_INVALID;
    }
    std::string data = "";
    char c;
    while (stream.get(c)) {
        data += c;
    }
    *contents = data;
    SPDLOG_TRACE("GCS: File {} has been downloaded (bytes={})", path,
        data.size());
    return StatusCode::OK;
}

StatusCode GCSFileSystem::downloadFile(const std::string& remote_path,
    const std::string& local_path) {
    SPDLOG_TRACE("GCS: Saving file {} to {}", remote_path, local_path);
    std::string contents;
    auto read_status = this->readTextFile(remote_path, &contents);
    if (read_status != StatusCode::OK) {
        SPDLOG_ERROR("Failed to get object at {}", remote_path);
        return read_status;
    }
    std::ofstream output_file(local_path.c_str(), std::ios::binary);
    output_file << contents;
    output_file.close();
    return StatusCode::OK;
}

StatusCode GCSFileSystem::downloadFileFolder(const std::string& path,
    std::string* local_path) {
    char dir_name_buf[] = "/tmp/ovms_model_dir_XXXXXX";
    char* ret = mkdtemp(dir_name_buf);
    if (ret == nullptr) {
        SPDLOG_ERROR("Failed to create local temp directory: {} error: {}",
            dir_name_buf, strerror(errno));
        return StatusCode::FILE_INVALID;
    }
    std::string created_dir_name = dir_name_buf;
    *local_path = created_dir_name;
    SPDLOG_INFO(
        "GCS: Downloading dir {} (recursive) and saving a new local path: {}",
        path, created_dir_name);
    return downloadFileFolderTo(path, created_dir_name);
}

StatusCode GCSFileSystem::downloadFileFolderTo(const std::string& path,
    const std::string& local_path) {
    SPDLOG_TRACE("GCS: Downloading dir {} and saving to {}", path, local_path);
    bool is_dir;
    auto status = this->isDirectory(path, &is_dir);
    if (status != StatusCode::OK) {
        SPDLOG_ERROR("File/folder does not exist at {}", path);
        return StatusCode::GCS_FILE_NOT_FOUND;
    }
    if (!is_dir) {
        SPDLOG_ERROR("Path is not a directory: {}", path);
        return StatusCode::GCS_FILE_NOT_FOUND;
    }

    std::set<std::string> dirs;
    status = getDirectorySubdirs(path, &dirs);
    if (status != StatusCode::OK) {
        return status;
    }

    std::set<std::string> files;
    status = getDirectoryFiles(path, &files);
    if (status != StatusCode::OK) {
        return status;
    }

    for (auto&& d : dirs) {
        std::string remote_dir_path = joinPath({path, d});
        std::string local_dir_path = joinPath({local_path, d});
        SPDLOG_TRACE("Processing directory {} from {} -> {}", d, remote_dir_path,
            local_dir_path);
        auto mkdir_status = CreateLocalDir(local_dir_path);
        if (mkdir_status != StatusCode::OK) {
            return status;
        }
        auto download_dir_status =
            this->downloadFileFolderTo(remote_dir_path, local_dir_path);
        if (download_dir_status != StatusCode::OK) {
            SPDLOG_ERROR("Unable to download directory from {} to {}",
                remote_dir_path, local_dir_path);
            return download_dir_status;
        }
    }

    for (auto&& f : files) {
        std::string remote_file_path = joinPath({path, f});
        std::string local_file_path = joinPath({local_path, f});
        SPDLOG_TRACE("Processing file {} from {} -> {}", f, remote_file_path,
            local_file_path);
        auto download_status =
            this->downloadFile(remote_file_path, local_file_path);
        if (download_status != StatusCode::OK) {
            SPDLOG_ERROR("Unable to save file from {} to {}", remote_file_path,
                local_file_path);
            return download_status;
        }
    }
    return StatusCode::OK;
}

StatusCode GCSFileSystem::deleteFileFolder(const std::string& path) {
    SPDLOG_DEBUG("GCS: deleting local file folder {}", path);
    if (::remove(path.c_str()) == 0) {
        return StatusCode::OK;
    } else {
        SPDLOG_INFO("GCS: unable to remove local path: {}", path);
        return StatusCode::FILE_INVALID;
    }
}

}  // namespace ovms
