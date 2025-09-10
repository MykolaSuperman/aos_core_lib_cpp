/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CORE_CM_IMAGEMANAGER_ITF_FILEINFOPROVIDER_HPP_
#define AOS_CORE_CM_IMAGEMANAGER_ITF_FILEINFOPROVIDER_HPP_

#include <core/common/tools/error.hpp>
#include <core/common/types/types.hpp>

namespace aos::cm::imagemanager {

/** @addtogroup cm Communication Manager
 *  @{
 */

/**
 * File info.
 */
struct FileInfo {
    /**
     * Constructor.
     */
    FileInfo() = default;

    StaticString<cSHA256Size> mSHA256;
    size_t                    mSize;
};

/**
 * File info provider.
 */
class FileInfoProviderItf {
public:
    /**
     * Destructor.
     */
    virtual ~FileInfoProviderItf() = default;

    /**
     * Creates file info.
     *
     * @param path file path.
     * @param[out] info file info.
     * @return Error.
     */
    virtual Error CreateFileInfo(const String& path, FileInfo& info) = 0;
};

} // namespace aos::cm::imagemanager

#endif // AOS_CORE_CM_IMAGEMANAGER_ITF_FILEINFOPROVIDER_HPP_
