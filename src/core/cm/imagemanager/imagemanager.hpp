/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CORE_CM_IMAGEMANAGER_IMAGEMANAGER_HPP_
#define AOS_CORE_CM_IMAGEMANAGER_IMAGEMANAGER_HPP_

#include <core/cm/fileserver/fileserver.hpp>
#include <core/cm/launcher/itf/imageinfoprovider.hpp>
#include <core/cm/smcontroller/itf/updateimageprovider.hpp>
#include <core/common/spaceallocator/spaceallocator.hpp>
#include <core/common/tools/identifierpool.hpp>
#include <core/common/tools/timer.hpp>
#include <core/common/types/types.hpp>

#include "itf/encoder.hpp"
#include "itf/fileinfoprovider.hpp"
#include "itf/imagedecrypter.hpp"
#include "itf/imagemanager.hpp"
#include "itf/statusnotifier.hpp"
#include "itf/storage.hpp"

namespace aos::cm::imagemanager {

/** @addtogroup cm Communication Manager
 *  @{
 */

/**
 * Image manager configuration.
 */
struct Config {
    StaticString<cFilePathLen> mInstallPath;
    Duration                   mItemTTL;

    /**
     * Compares config.
     *
     * @param other config to compare.
     * @return bool.
     */
    bool operator==(const Config& other) const
    {
        return mInstallPath == other.mInstallPath && mItemTTL == other.mItemTTL;
    }

    /**
     * Compares config.
     *
     * @param other config to compare.
     * @return bool.
     */
    bool operator!=(const Config& other) const { return !operator==(other); }
};

/**
 * Image manager.
 */
class ImageManager : public ImageManagerItf,
                     public StatusNotifierItf,
                     public smcontroller::UpdateImageProviderItf,
                     public launcher::ImageInfoProviderItf,
                     public spaceallocator::ItemRemoverItf {
public:
    /**
     * Initializes image manager.
     *
     * @param storage image storage.
     * @return Error.
     */
    Error Init(const Config& config, storage::StorageItf& storage, spaceallocator::SpaceAllocatorItf& spaceAllocator,
        EncoderItf& encoder, fileserver::FileServerItf& fileserver, ImageDecrypterItf& imageDecrypter,
        FileInfoProviderItf& fileInfoProvider);

    /**
     * Starts image manager.
     *
     * @return Error.
     */
    Error Start();

    /**
     * Stops image manager.
     *
     * @return Error.
     */
    Error Stop();

    /**
     * Retrieves update items statuses.
     *
     * @param[out] statuses list of update items statuses.
     * @return Error.
     */
    Error GetUpdateItemsStatuses(Array<UpdateItemStatus>& statuses) override;

    /**
     *
     * Installs update item.
     *
     * @param itemInfo update item info.
     * @param certificates list of certificates.
     * @param certificateChains list of certificate chains.
     * @param[out] status update item status.
     * @return Error.
     */
    Error InstallUpdateItems(const Array<UpdateItemInfo>& itemsInfo,
        const Array<cloudprotocol::CertificateInfo>&      certificates,
        const Array<cloudprotocol::CertificateChainInfo>& certificateChains,
        Array<UpdateItemStatus>&                          statuses) override;

    /**
     * Uninstalls update item.
     *
     * @param ids update item ID's.
     * @return Error.
     */
    Error UninstallUpdateItems(const Array<uuid::UUID>& ids, Array<UpdateItemStatus>& statuses) override;

    /**
     * Reverts update item.
     *
     * @param ids update item ID's.
     * @return Error.
     */
    Error RevertUpdateItems(const Array<uuid::UUID>& ids, Array<UpdateItemStatus>& statuses) override;

    /**
     * Subscribes status notifications.
     *
     * @param listener status listener.
     * @return Error.
     */
    Error SubscribeListener(StatusListenerItf& listener) override;

    /**
     * Unsubscribes from status notifications.
     *
     * @param listener status listener.
     * @return Error.
     */
    Error UnsubscribeListener(StatusListenerItf& listener) override;

    /**
     * Returns update image info for desired platform.
     *
     * @param id update item ID.
     * @param platform platform information.
     * @param[out] info update image info.
     * @return Error.
     */
    Error GetUpdateImageInfo(
        const uuid::UUID& id, const PlatformInfo& platform, smcontroller::UpdateImageInfo& info) override;

    /**
     * Returns layer image info.
     *
     * @param digest layer digest.
     * @param info layer image info.
     * @return Error.
     */
    Error GetLayerImageInfo(const String& digest, smcontroller::UpdateImageInfo& info) override;

    /**
     * Returns update item version.
     *
     * @param id update item ID.
     * @return RetWithError<StaticString<cVersionLen>>.
     */
    RetWithError<StaticString<cVersionLen>> GetItemVersion(const uuid::UUID& id) override;

    /**
     * Returns update item images infos.
     *
     * @param id update item ID.
     * @param[out] imagesInfos update item images info.
     * @return Error.
     */
    Error GetItemImages(const uuid::UUID& id, Array<ImageInfo>& imagesInfos) override;

    /**
     * Returns service config.
     *
     * @param id update item ID.
     * @param imageID image ID.
     * @param config service config.
     * @return Error.
     */
    Error GetServiceConfig(const uuid::UUID& id, const uuid::UUID& imageID, oci::ServiceConfig& config) override;

    /**
     * Returns image config.
     *
     * @param id update item ID.
     * @param imageID image ID.
     * @param config image config.
     * @return Error.
     */
    Error GetImageConfig(const uuid::UUID& id, const uuid::UUID& imageID, oci::ImageConfig& config) override;

    /**
     * Removes item.
     *
     * @param id item id.
     * @return Error.
     */
    Error RemoveItem(const String& id) override;

private:
    static constexpr auto cNumInstallThreads = AOS_CONFIG_IMAGEMANAGER_NUM_COOPERATE_INSTALLS;
    static constexpr auto cRemovePeriod      = 24 * Time::cHours;

    Error InstallUpdateItem(const UpdateItemInfo& itemInfo, const Array<cloudprotocol::CertificateInfo>& certificates,
        const Array<cloudprotocol::CertificateChainInfo>& certificateChains, UpdateItemStatus& status);
    Error ValidateActiveVersionItem(const UpdateItemInfo& itemInfo, const Array<storage::ItemInfo>& items);
    Error ValidateCachedVersionItem(const UpdateItemInfo& itemInfo, const Array<storage::ItemInfo>& items);
    Error SetState(const storage::ItemInfo& item, storage::ItemState state);
    Error Remove(const storage::ItemInfo& item);
    void  ReleaseAllocatedSpace(const String& path, spaceallocator::SpaceItf* space);
    void  AcceptAllocatedSpace(spaceallocator::SpaceItf* space);
    Error InstallItem(const UpdateImageInfo& imageInfo, const String& installPath, const String& imagePath,
        storage::ImageInfo& image, const Array<cloudprotocol::CertificateChainInfo>& certificateChains,
        const Array<cloudprotocol::CertificateInfo>& certificates);
    Error DecryptAndValidate(const UpdateImageInfo& imageInfo, const String& installPath,
        StaticString<cFilePathLen>&                       outDecryptedFile,
        const Array<cloudprotocol::CertificateChainInfo>& certificateChains,
        const Array<cloudprotocol::CertificateInfo>&      certificates);
    Error PrepareUrlsAndFileInfo(const String& imagePath, const String& decryptedFile, const UpdateImageInfo& imageInfo,
        storage::ImageInfo& image);
    Error UpdatePrevVersions(const Array<storage::ItemInfo>& items);
    Error UninstallUpdateItem(const uuid::UUID& id, UpdateItemStatus& status);
    Error RevertUpdateItem(const uuid::UUID& id, Array<UpdateItemStatus>& statuses);
    Error SetItemStatus(
        const Array<storage::ImageInfo>& itemImages, UpdateItemStatus& status, ImageState state, Error error);
    Error SetOutdatedItems();
    Error RemoveOutdatedItems();

    storage::StorageItf*               mStorage {};
    spaceallocator::SpaceAllocatorItf* mSpaceAllocator {};
    EncoderItf*                        mEncoder {};
    fileserver::FileServerItf*         mFileServer {};
    ImageDecrypterItf*                 mImageDecrypter {};
    FileInfoProviderItf*               mFileInfoProvider {};

    StaticArray<StatusListenerItf*, cMaxNumListeners> mListeners;

    Timer  mTimer;
    Config mConfig {};

    StaticAllocator<sizeof(StaticArray<storage::ItemInfo, cMaxNumUpdateItems>) * cNumInstallThreads> mAllocator;

    ThreadPool<cNumInstallThreads, cMaxNumUpdateItems> mInstallPool;
};

} // namespace aos::cm::imagemanager

#endif // AOS_CORE_CM_IMAGEMANAGER_IMAGEMANAGER_HPP_
