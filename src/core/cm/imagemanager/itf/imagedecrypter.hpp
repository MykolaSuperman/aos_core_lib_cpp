/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CORE_CM_IMAGEMANAGER_ITF_IMAGEDECRYPTER_HPP_
#define AOS_CORE_CM_IMAGEMANAGER_ITF_IMAGEDECRYPTER_HPP_

#include <core/common/cloudprotocol/desiredstatus.hpp>
#include <core/common/types/types.hpp>

namespace aos::cm::imagemanager {

/** @addtogroup cm Communication Manager
 *  @{
 */

/**
 * Image decrypter interface.
 */
class ImageDecrypterItf {
public:
    /**
     * Destructor.
     */
    virtual ~ImageDecrypterItf() = default;

    /**
     * Decrypts image.
     *
     * @param encryptedPath encrypted path.
     * @param decryptedPath decrypted path.
     * @param decryptionInfo decryption info.
     * @return Error.
     */
    virtual Error Decrypt(
        const String& encryptedPath, const String& decryptedPath, const cloudprotocol::DecryptInfo& decryptionInfo)
        = 0;

    /**
     * Validates signs.
     *
     * @param decryptedPath decrypted path.
     * @param signs signs.
     * @param chains chains.
     * @param certs certs.
     * @return Error.
     */
    virtual Error ValidateSigns(const String& decryptedPath, const cloudprotocol::SignInfo& signs,
        const Array<cloudprotocol::CertificateChainInfo>& chains, const Array<cloudprotocol::CertificateInfo>& certs)
        = 0;
};

} // namespace aos::cm::imagemanager

#endif // AOS_CORE_CM_IMAGEMANAGER_ITF_IMAGEDECRYPTER_HPP_
