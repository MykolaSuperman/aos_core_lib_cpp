/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <core/common/tools/logger.hpp>
#include <core/common/tools/semver.hpp>

#include "imagemanager.hpp"

namespace aos::cm::imagemanager {

/**********************************************************************************************************************
Public
***********************************************************************************************************************/

Error ImageManager::Init(const Config& config, storage::StorageItf& storage,
    spaceallocator::SpaceAllocatorItf& spaceAllocator, EncoderItf& encoder, fileserver::FileServerItf& fileserver,
    ImageDecrypterItf& imageDecrypter, FileInfoProviderItf& fileInfoProvider)
{
    LOG_DBG() << "Init image manager";

    mConfig           = config;
    mStorage          = &storage;
    mSpaceAllocator   = &spaceAllocator;
    mEncoder          = &encoder;
    mFileServer       = &fileserver;
    mImageDecrypter   = &imageDecrypter;
    mFileInfoProvider = &fileInfoProvider;

    return SetOutdatedItems();
}

Error ImageManager::Start()
{
    LOG_DBG() << "Start image manager";

    if (auto err = mInstallPool.Run(); !err.IsNone()) {
        return err;
    }

    if (auto err = RemoveOutdatedItems(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mTimer.Start(
            cRemovePeriod,
            [this](void*) {
                if (auto err = RemoveOutdatedItems(); !err.IsNone()) {
                    LOG_ERR() << "Error removing outdated items: err=" << err;
                }
            },
            false);
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error ImageManager::Stop()
{
    LOG_DBG() << "Stop image manager";

    mInstallPool.Shutdown();

    return mTimer.Stop();
}

Error ImageManager::GetUpdateItemsStatuses(Array<UpdateItemStatus>& statuses)
{
    LOG_DBG() << "Get update items statuses";

    auto items = MakeUnique<StaticArray<storage::ItemInfo, storage::cMaxItemVersions>>(&mAllocator);

    if (auto err = mStorage->GetItemsInfo(*items); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    for (const auto& item : *items) {
        if (item.mState != storage::ItemStateEnum::eActive) {
            continue;
        }

        if (auto err = statuses.EmplaceBack(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        auto& status = statuses.Back();

        status.mID      = item.mID;
        status.mVersion = item.mVersion;

        for (const auto& image : item.mItems) {
            if (auto err = status.mStatuses.EmplaceBack(); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }

            auto& imageStatus = status.mStatuses.Back();

            imageStatus.mImageID = image.mImageID;
            imageStatus.mStatus  = ImageStateEnum::eInstalled;
        }
    }

    return ErrorEnum::eNone;
}

Error ImageManager::InstallUpdateItems(const Array<UpdateItemInfo>& itemsInfo,
    const Array<cloudprotocol::CertificateInfo>&                    certificates,
    const Array<cloudprotocol::CertificateChainInfo>& certificateChains, Array<UpdateItemStatus>& statuses)
{
    LOG_DBG() << "Install update items" << Log::Field("Items", itemsInfo.Size());

    if (auto err = statuses.Resize(itemsInfo.Size()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    for (size_t i = 0; i < itemsInfo.Size(); ++i) {
        auto& itemInfo = itemsInfo[i];
        auto& status   = statuses[i];

        auto err = mInstallPool.AddTask([this, &itemInfo, &certificates, &certificateChains, &status](void*) {
            if (auto err = InstallUpdateItem(itemInfo, certificates, certificateChains, status); !err.IsNone()) {
                LOG_ERR() << "Can't install update item" << Log::Field("ID", uuid::UUIDToString(itemInfo.mID))
                          << Log::Field("Version", itemInfo.mVersion) << Log::Field("Error", err);
            }
        });

        if (!err.IsNone()) {
            LOG_ERR() << "Can't add update item install task" << Log::Field("ID", uuid::UUIDToString(itemInfo.mID))
                      << Log::Field("Version", itemInfo.mVersion) << Log::Field("Error", err);
        }
    }

    mInstallPool.Wait();
    mInstallPool.Shutdown();

    return ErrorEnum::eNone;
}

Error ImageManager::UninstallUpdateItems(const Array<uuid::UUID>& ids, Array<UpdateItemStatus>& statuses)
{
    LOG_DBG() << "Uninstall update items" << Log::Field("Items", ids.Size());

    if (auto err = statuses.Resize(ids.Size()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    for (size_t i = 0; i < ids.Size(); ++i) {
        auto& id     = ids[i];
        auto& status = statuses[i];

        auto err = mInstallPool.AddTask([this, &id, &status](void*) {
            if (auto err = UninstallUpdateItem(id, status); !err.IsNone()) {
                LOG_ERR() << "Can't uninstall update item" << Log::Field("ID", uuid::UUIDToString(id))
                          << Log::Field("Error", err);
            }
        });

        if (!err.IsNone()) {
            LOG_ERR() << "Can't add update item uninstall task" << Log::Field("ID", uuid::UUIDToString(id))
                      << Log::Field("Error", err);
        }
    }

    mInstallPool.Wait();
    mInstallPool.Shutdown();

    return ErrorEnum::eNone;
}

Error ImageManager::RevertUpdateItems(const Array<uuid::UUID>& ids, Array<UpdateItemStatus>& statuses)
{
    for (const auto& id : ids) {
        auto err = mInstallPool.AddTask([this, &id, &statuses](void*) {
            if (auto err = RevertUpdateItem(id, statuses); !err.IsNone()) {
                LOG_ERR() << "Can't revert update item" << Log::Field("ID", uuid::UUIDToString(id))
                          << Log::Field("Error", err);
            }
        });

        if (!err.IsNone()) {
            LOG_ERR() << "Can't add update item revert task" << Log::Field("ID", uuid::UUIDToString(id))
                      << Log::Field("Error", err);
        }
    }

    mInstallPool.Wait();
    mInstallPool.Shutdown();

    return ErrorEnum::eNone;
}

Error ImageManager::SubscribeListener(StatusListenerItf& listener)
{
    LOG_DBG() << "Subscribe listener";

    auto it = mListeners.Find(&listener);

    if (it != mListeners.end()) {
        return ErrorEnum::eAlreadyExist;
    }

    return AOS_ERROR_WRAP(mListeners.PushBack(&listener));
}

Error ImageManager::UnsubscribeListener(StatusListenerItf& listener)
{
    LOG_DBG() << "Unsubscribe listener";

    return mListeners.Remove(&listener) > 0 ? ErrorEnum::eNone : ErrorEnum::eNotFound;
}

Error ImageManager::GetUpdateImageInfo(
    const uuid::UUID& id, const PlatformInfo& platform, smcontroller::UpdateImageInfo& info)
{
    LOG_DBG() << "Get update image info" << Log::Field("ID", uuid::UUIDToString(id))
              << Log::Field("Architecture", platform.mArchInfo.mArchitecture) << Log::Field("OS", platform.mOSInfo.mOS);

    auto items = MakeUnique<StaticArray<storage::ItemInfo, storage::cMaxItemVersions>>(&mAllocator);

    if (auto err = mStorage->GetItemVersionsByURN(id, *items); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    auto it = items->FindIf(
        [&id](const storage::ItemInfo& item) { return item.mState == storage::ItemStateEnum::eActive; });
    if (it == items->end()) {
        return ErrorEnum::eNotFound;
    }

    for (const auto& image : it->mItems) {
        if (image.mArchInfo == platform.mArchInfo && image.mOSInfo == platform.mOSInfo) {
            info.mImageID = image.mImageID;
            info.mVersion = it->mVersion;
            info.mURL     = image.mURL;
            info.mSHA256  = image.mSHA256;
            info.mSize    = image.mSize;

            return ErrorEnum::eNone;
        }
    }

    return ErrorEnum::eNotFound;
}

Error ImageManager::GetLayerImageInfo(
    [[maybe_unused]] const String& digest, [[maybe_unused]] smcontroller::UpdateImageInfo& info)
{
    return ErrorEnum::eNone;
}

RetWithError<StaticString<cVersionLen>> ImageManager::GetItemVersion(const uuid::UUID& id)
{
    LOG_DBG() << "Get item version" << Log::Field("ID", uuid::UUIDToString(id));

    auto items = MakeUnique<StaticArray<storage::ItemInfo, storage::cMaxItemVersions>>(&mAllocator);

    if (auto err = mStorage->GetItemVersionsByURN(id, *items); !err.IsNone()) {
        return {StaticString<cVersionLen> {}, AOS_ERROR_WRAP(err)};
    }

    auto it = items->FindIf(
        [&id](const storage::ItemInfo& item) { return item.mState == storage::ItemStateEnum::eActive; });
    if (it == items->end()) {
        return {StaticString<cVersionLen> {}, ErrorEnum::eNotFound};
    }

    return {it->mVersion, ErrorEnum::eNone};
}

Error ImageManager::GetItemImages(const uuid::UUID& id, Array<ImageInfo>& imagesInfos)
{
    LOG_DBG() << "Get item images" << Log::Field("ID", uuid::UUIDToString(id));

    auto items = MakeUnique<StaticArray<storage::ItemInfo, storage::cMaxItemVersions>>(&mAllocator);

    if (auto err = mStorage->GetItemVersionsByURN(id, *items); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    for (const auto& item : *items) {
        if (item.mState != storage::ItemStateEnum::eActive) {
            continue;
        }

        for (const auto& image : item.mItems) {
            if (auto err = imagesInfos.EmplaceBack(); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }

            auto& imageInfo = imagesInfos.Back();

            imageInfo.mImageID  = image.mImageID;
            imageInfo.mArchInfo = image.mArchInfo;
            imageInfo.mOSInfo   = image.mOSInfo;
        }
    }

    return ErrorEnum::eNone;
}

Error ImageManager::GetServiceConfig([[maybe_unused]] const uuid::UUID& id, [[maybe_unused]] const uuid::UUID& imageID,
    [[maybe_unused]] oci::ServiceConfig& config)
{
    return ErrorEnum::eNone;
}

Error ImageManager::GetImageConfig([[maybe_unused]] const uuid::UUID& id, [[maybe_unused]] const uuid::UUID& imageID,
    [[maybe_unused]] oci::ImageConfig& config)
{
    return ErrorEnum::eNone;
}

Error ImageManager::RemoveItem(const String& id)
{
    LOG_DBG() << "Remove item" << Log::Field("ID", id);

    for (const auto& listener : mListeners) {
        auto [uuid, err] = uuid::StringToUUID(id);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        listener->OnUpdateItemRemoved(uuid);
    }

    auto items = MakeUnique<StaticArray<storage::ItemInfo, storage::cMaxItemVersions>>(&mAllocator);

    auto [uuid, err] = uuid::StringToUUID(id);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = mStorage->GetItemVersionsByURN(uuid, *items); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    for (const auto& item : *items) {
        if (auto err = Remove(item); !err.IsNone()) {
            return err;
        }
    }

    return ErrorEnum::eNone;
}

/**********************************************************************************************************************
Private
***********************************************************************************************************************/

Error ImageManager::UninstallUpdateItem(const uuid::UUID& id, UpdateItemStatus& status)
{
    LOG_DBG() << "Uninstall update item" << Log::Field("ID", uuid::UUIDToString(id));

    auto items = MakeUnique<StaticArray<storage::ItemInfo, storage::cMaxItemVersions>>(&mAllocator);

    if (auto err = mStorage->GetItemVersionsByURN(id, *items); !err.IsNone()) {
        return err;
    }

    Error err;

    for (const auto& item : *items) {
        status.mID      = item.mID;
        status.mVersion = item.mVersion;

        auto setItemStatus = DeferRelease(&err, [&](Error* err) {
            if (auto errStatus = SetItemStatus(item.mItems, status, ImageStateEnum::eRemoved, *err);
                !errStatus.IsNone()) {
                LOG_ERR() << "Can't set update item status" << Log::Field("ID", uuid::UUIDToString(item.mID))
                          << Log::Field("Version", item.mVersion) << Log::Field("Error", errStatus);

                return;
            }
        });

        switch (item.mState.GetValue()) {
        case storage::ItemStateEnum::eActive:
            if (auto err = SetState(item, storage::ItemStateEnum::eCached); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }

            break;

        case storage::ItemStateEnum::eCached:
            if (auto err = Remove(item); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }

            break;

        default:
            return AOS_ERROR_WRAP(ErrorEnum::eInvalidArgument);
        }
    }

    return ErrorEnum::eNone;
}

Error ImageManager::RevertUpdateItem(const uuid::UUID& id, Array<UpdateItemStatus>& statuses)
{
    LOG_DBG() << "Revert update item" << Log::Field("ID", uuid::UUIDToString(id));

    auto items = MakeUnique<StaticArray<storage::ItemInfo, storage::cMaxItemVersions>>(&mAllocator);

    if (auto err = mStorage->GetItemVersionsByURN(id, *items); !err.IsNone()) {
        return err;
    }

    auto itActive
        = items->FindIf([](const storage::ItemInfo& item) { return item.mState == storage::ItemStateEnum::eActive; });

    auto itCached
        = items->FindIf([](const storage::ItemInfo& item) { return item.mState == storage::ItemStateEnum::eCached; });

    auto createStatus = [&](const uuid::UUID& id, const String& version) -> RetWithError<UpdateItemStatus> {
        if (auto err = statuses.EmplaceBack(); !err.IsNone()) {
            return {UpdateItemStatus {}, AOS_ERROR_WRAP(err)};
        }

        auto& status = statuses.Back();

        status.mID      = id;
        status.mVersion = version;

        return {status, ErrorEnum::eNone};
    };
    if (itActive != items->end()) {
        {
            auto [status, err] = createStatus(itActive->mID, itActive->mVersion);
            if (!err.IsNone()) {
                return err;
            }

            err = Remove(*itActive);

            if (auto errStatus = SetItemStatus(itActive->mItems, status, ImageStateEnum::eRemoved, err);
                !errStatus.IsNone()) {
                LOG_ERR() << "Can't set update item status" << Log::Field("ID", uuid::UUIDToString(itActive->mID))
                          << Log::Field("Version", itActive->mVersion) << Log::Field("Error", errStatus);

                return errStatus;
            }

            if (!err.IsNone()) {
                return err;
            }
        }

        if (itCached != items->end()) {
            {
                auto [status, err] = createStatus(itCached->mID, itCached->mVersion);
                if (!err.IsNone()) {
                    return err;
                }

                err = SetState(*itCached, storage::ItemStateEnum::eActive);

                if (auto errStatus = SetItemStatus(itCached->mItems, status, ImageStateEnum::eInstalled, err);
                    !errStatus.IsNone()) {
                    LOG_ERR() << "Can't set update item status" << Log::Field("ID", uuid::UUIDToString(itCached->mID))
                              << Log::Field("Version", itCached->mVersion) << Log::Field("Error", errStatus);

                    return errStatus;
                }

                return err;
            }
        }

        return ErrorEnum::eNone;
    }

    return ErrorEnum::eNone;
}

Error ImageManager::SetItemStatus(
    const Array<storage::ImageInfo>& itemImages, UpdateItemStatus& status, ImageState state, Error error)
{
    for (const auto& itemImage : itemImages) {
        if (auto err = status.mStatuses.EmplaceBack(); !err.IsNone()) {
            LOG_ERR() << "Can't add update item status" << Log::Field("ID", uuid::UUIDToString(status.mID))
                      << Log::Field("Version", status.mVersion) << Log::Field("Error", err);
            return err;
        }

        auto& itemStatus = status.mStatuses.Back();

        itemStatus.mImageID = itemImage.mImageID;
        itemStatus.mStatus  = error.IsNone() ? state.GetValue() : ImageStateEnum::eFailed;
        itemStatus.mError   = error;
    }

    return ErrorEnum::eNone;
}

Error ImageManager::InstallUpdateItem(const UpdateItemInfo& itemInfo,
    const Array<cloudprotocol::CertificateInfo>&            certificates,
    const Array<cloudprotocol::CertificateChainInfo>& certificateChains, UpdateItemStatus& status)
{
    LOG_DBG() << "Install update item" << Log::Field("ID", uuid::UUIDToString(itemInfo.mID))
              << Log::Field("Version", itemInfo.mVersion) << Log::Field("Images", itemInfo.mImages.Size());

    auto items = MakeUnique<StaticArray<storage::ItemInfo, storage::cMaxItemVersions>>(&mAllocator);

    if (auto err = mStorage->GetItemVersionsByURN(itemInfo.mID, *items); !err.IsNone()) {
        return err;
    }

    if (auto err = ValidateActiveVersionItem(itemInfo, *items); !err.IsNone()) {
        if (err.Is(ErrorEnum::eAlreadyExist)) {
            return ErrorEnum::eNone;
        }

        if (!err.Is(ErrorEnum::eNotFound)) {
            return err;
        }

        if (err = ValidateCachedVersionItem(itemInfo, *items); !err.IsNone()) {
            if (err.Is(ErrorEnum::eAlreadyExist)) {
                return ErrorEnum::eNone;
            }

            if (!err.Is(ErrorEnum::eNotFound)) {
                return err;
            }
        }
    }

    size_t totalSize {};
    for (const auto& image : itemInfo.mImages) {
        totalSize += image.mSize;
    }

    auto [space, err] = mSpaceAllocator->AllocateSpace(totalSize);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    auto itemPath    = fs::JoinPath("items", itemInfo.mVersion);
    auto installPath = fs::JoinPath(mConfig.mInstallPath, itemPath);

    // cppcheck-suppress constParameterPointer
    auto releaseItemSpace = DeferRelease(&err, [&](Error* err) {
        if (!err->IsNone()) {
            LOG_ERR() << "Can't install item" << Log::Field("ID", uuid::UUIDToString(itemInfo.mID))
                      << Log::Field("version", itemInfo.mVersion) << Log::Field(*err);

            ReleaseAllocatedSpace(installPath, space.Get());

            return;
        }

        AcceptAllocatedSpace(space.Get());
    });

    auto item = MakeUnique<storage::ItemInfo>(&mAllocator);

    item->mID        = itemInfo.mID;
    item->mVersion   = itemInfo.mVersion;
    item->mState     = storage::ItemStateEnum::eActive;
    item->mPath      = installPath;
    item->mTotalSize = totalSize;
    item->mTimestamp = Time::Now();

    status.mID      = itemInfo.mID;
    status.mVersion = itemInfo.mVersion;

    for (const auto& image : itemInfo.mImages) {
        if (err = status.mStatuses.EmplaceBack(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        auto& itemStatus    = status.mStatuses.Back();
        itemStatus.mImageID = image.mImage.mImageID;

        auto setItemStatus = DeferRelease(&err, [&](Error* err) {
            if (!err->IsNone()) {
                itemStatus.mStatus = ImageStateEnum::eFailed;
            } else {
                itemStatus.mStatus = ImageStateEnum::eInstalled;
            }

            itemStatus.mError = *err;
        });

        if (err = item->mItems.EmplaceBack(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (err = InstallItem(image, installPath, itemPath, item->mItems.Back(), certificateChains, certificates);
            !err.IsNone()) {
            return err;
        }
    }

    if (err = UpdatePrevVersions(*items); !err.IsNone()) {
        return err;
    }

    if (err = mStorage->AddItem(*item); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error ImageManager::ValidateActiveVersionItem(const UpdateItemInfo& itemInfo, const Array<storage::ItemInfo>& items)
{
    LOG_DBG() << "Validate active version item" << Log::Field("ID", uuid::UUIDToString(itemInfo.mID))
              << Log::Field("version", itemInfo.mVersion);

    auto it = items.FindIf(
        [&itemInfo](const storage::ItemInfo& item) { return item.mState == storage::ItemStateEnum::eActive; });

    if (it == items.end()) {
        return ErrorEnum::eNotFound;
    }

    auto [versionResult, err] = semver::CompareSemver(itemInfo.mVersion, it->mVersion);

    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (versionResult == 0) {
        return ErrorEnum::eAlreadyExist;
    }

    if (versionResult < 0) {
        return ErrorEnum::eWrongState;
    }

    return ErrorEnum::eNone;
}

Error ImageManager::ValidateCachedVersionItem(const UpdateItemInfo& itemInfo, const Array<storage::ItemInfo>& items)
{
    LOG_DBG() << "Validate cached version item" << Log::Field("ID", uuid::UUIDToString(itemInfo.mID))
              << Log::Field("version", itemInfo.mVersion);

    auto it = items.FindIf(
        [&itemInfo](const storage::ItemInfo& item) { return item.mState == storage::ItemStateEnum::eCached; });

    if (it == items.end()) {
        return ErrorEnum::eNotFound;
    }

    auto [versionResult, err] = semver::CompareSemver(itemInfo.mVersion, it->mVersion);

    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (versionResult == 0) {
        if (auto err = SetState(*it, storage::ItemStateEnum::eActive); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        return ErrorEnum::eAlreadyExist;
    }

    if (versionResult > 0) {
        if (auto err = Remove(*it); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

Error ImageManager::SetState(const storage::ItemInfo& item, storage::ItemState state)
{
    if (auto err = mStorage->SetItemState(item.mID, item.mVersion, state); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    auto id = uuid::UUIDToString(item.mID);

    if (state == storage::ItemStateEnum::eCached) {
        if (auto err = mSpaceAllocator->AddOutdatedItem(id, item.mTotalSize, item.mTimestamp); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    if (item.mState == storage::ItemStateEnum::eCached) {
        if (auto err = mSpaceAllocator->RestoreOutdatedItem(id); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

Error ImageManager::Remove(const storage::ItemInfo& item)
{
    LOG_DBG() << "Remove item" << Log::Field("ID", uuid::UUIDToString(item.mID)) << Log::Field("Path", item.mPath);

    for (const auto& listener : mListeners) {
        listener->OnUpdateItemRemoved(item.mID);
    }

    if (auto err = fs::RemoveAll(item.mPath); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    auto id = uuid::UUIDToString(item.mID);

    if (item.mState == storage::ItemStateEnum::eCached) {
        if (auto err = mSpaceAllocator->RestoreOutdatedItem(id); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    mSpaceAllocator->FreeSpace(item.mTotalSize);

    if (auto err = mStorage->RemoveItem(item.mID, item.mVersion); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    LOG_DBG() << "Item removed" << Log::Field("ID", uuid::UUIDToString(item.mID))
              << Log::Field("Version", item.mVersion);

    return ErrorEnum::eNone;
}

void ImageManager::ReleaseAllocatedSpace(const String& path, spaceallocator::SpaceItf* space)
{
    if (!path.IsEmpty()) {
        fs::RemoveAll(path);
    }

    if (auto err = space->Release(); !err.IsNone()) {
        LOG_ERR() << "Can't release item space: err=" << err;
    }
}

void ImageManager::AcceptAllocatedSpace(spaceallocator::SpaceItf* space)
{
    if (auto err = space->Accept(); !err.IsNone()) {
        LOG_ERR() << "Can't accept item space: err=" << err;
    }
}

Error ImageManager::InstallItem(const UpdateImageInfo& imageInfo, const String& installPath, const String& layerPath,
    storage::ImageInfo& image, const Array<cloudprotocol::CertificateChainInfo>& certificateChains,
    const Array<cloudprotocol::CertificateInfo>& certificates)
{
    // 1) Decrypt & validate
    StaticString<cFilePathLen> decryptedFile;
    if (auto err = DecryptAndValidate(imageInfo, installPath, decryptedFile, certificateChains, certificates);
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    // Basic identification of the layer from image
    image.mImageID = imageInfo.mImage.mImageID;
    image.mPath    = decryptedFile;

    // 2) Prepare URLs & file info
    if (auto err = PrepareUrlsAndFileInfo(layerPath, decryptedFile, imageInfo, image); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error ImageManager::DecryptAndValidate(const UpdateImageInfo& imageInfo, const String& installPath,
    StaticString<cFilePathLen>& outDecryptedFile, const Array<cloudprotocol::CertificateChainInfo>& certificateChains,
    const Array<cloudprotocol::CertificateInfo>& certificates)
{
    StaticString<cSHA256Base64Size> encodedURL;
    if (auto err = mEncoder->EncodeUrl(imageInfo.mSHA256, encodedURL); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    outDecryptedFile = fs::JoinPath(installPath, encodedURL);

    if (auto err = mImageDecrypter->Decrypt(imageInfo.mPath, outDecryptedFile, imageInfo.mDecryptInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err
        = mImageDecrypter->ValidateSigns(outDecryptedFile, imageInfo.mSignInfo, certificateChains, certificates);
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error ImageManager::PrepareUrlsAndFileInfo(
    const String& imagePath, const String& decryptedFile, const UpdateImageInfo& imageInfo, storage::ImageInfo& image)
{
    StaticString<cFilePathLen> remoteURL;
    StaticString<cFilePathLen> localURL;
    StaticString<cFilePathLen> imageBaseName;

    if (auto err = fs::BaseName(decryptedFile, imageBaseName); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mFileServer->TranslateURL(false, fs::JoinPath(imagePath, imageBaseName), remoteURL); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    FileInfo fileInfo;
    if (auto err = mFileInfoProvider->CreateFileInfo(decryptedFile, fileInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    image.mURL      = remoteURL;
    image.mSize     = fileInfo.mSize;
    image.mSHA256   = fileInfo.mSHA256;
    image.mArchInfo = imageInfo.mImage.mArchInfo;
    image.mOSInfo   = imageInfo.mImage.mOSInfo;

    return ErrorEnum::eNone;
}

Error ImageManager::UpdatePrevVersions(const Array<storage::ItemInfo>& items)
{
    auto itActive
        = items.FindIf([&](const storage::ItemInfo& item) { return item.mState == storage::ItemStateEnum::eActive; });

    if (itActive != items.end()) {
        if (auto err = SetState(*itActive, storage::ItemStateEnum::eCached); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    auto itCached
        = items.FindIf([&](const storage::ItemInfo& item) { return item.mState == storage::ItemStateEnum::eCached; });

    if (itCached != items.end()) {
        if (auto err = Remove(*itCached); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

Error ImageManager::SetOutdatedItems()
{
    auto items = MakeUnique<StaticArray<storage::ItemInfo, cMaxNumUpdateItems>>(&mAllocator);

    if (auto err = mStorage->GetItemsInfo(*items); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    for (const auto& item : *items) {
        if (item.mState == storage::ItemStateEnum::eCached) {
            auto id = uuid::UUIDToString(item.mID);

            if (auto err = mSpaceAllocator->AddOutdatedItem(id, item.mTotalSize, item.mTimestamp); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }
        }
    }

    return ErrorEnum::eNone;
}

Error ImageManager::RemoveOutdatedItems()
{
    auto items = MakeUnique<StaticArray<storage::ItemInfo, cMaxNumUpdateItems>>(&mAllocator);

    if (auto err = mStorage->GetItemsInfo(*items); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    for (const auto& item : *items) {
        if (item.mState == storage::ItemStateEnum::eCached && item.mTimestamp.Add(mConfig.mItemTTL) < Time::Now()) {
            if (auto err = Remove(item); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }
        }
    }

    return ErrorEnum::eNone;
}

} // namespace aos::cm::imagemanager
