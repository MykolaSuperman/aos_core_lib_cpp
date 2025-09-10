/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CORE_CM_IMAGEMANAGER_ITF_ENCODER_HPP_
#define AOS_CORE_CM_IMAGEMANAGER_ITF_ENCODER_HPP_

#include <core/common/tools/string.hpp>

namespace aos::cm::imagemanager {

/** @addtogroup cm Communication Manager
 *  @{
 */

constexpr size_t cSHA256Base64Size = 44;

/**
 * Encoder interface.
 */
class EncoderItf {
public:
    /**
     * Destructor.
     *
     */
    virtual ~EncoderItf() = default;

    /**
     * Encodes data to URL.
     *
     * @param data data to encode.
     * @param[out] url encoded URL.
     * @return Error.
     */
    virtual Error EncodeUrl(const String& data, String& url) = 0;
};

} // namespace aos::cm::imagemanager

#endif // AOS_CORE_CM_IMAGEMANAGER_ITF_ENCODER_HPP_
