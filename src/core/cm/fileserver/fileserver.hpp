/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CORE_CM_FILESERVER_HPP_
#define AOS_CORE_CM_FILESERVER_HPP_

#include <core/common/types/types.hpp>

namespace aos::cm::fileserver {

/** @addtogroup cm Communication Manager
 *  @{
 */

/**
 * File server interface.
 */
class FileServerItf {
public:
    /**
     * Destructor.
     */
    virtual ~FileServerItf() = default;

    /**
     * Translates URL.
     *
     * @param isLocal is local.
     * @param inURL input URL.
     * @param[out] outURL translated URL.
     * @return Error.
     */
    virtual Error TranslateURL(bool isLocal, const String& inURL, String& outURL) = 0;
};

} // namespace aos::cm::fileserver

#endif // AOS_CORE_CM_FILESERVER_HPP_
