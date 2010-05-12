/* vim: set sw=2 :miv */
/*
 *=BEGIN SONGBIRD GPL
 *
 * This file is part of the Songbird web player.
 *
 * Copyright(c) 2005-2010 POTI, Inc.
 * http://www.songbirdnest.com
 *
 * This file may be licensed under the terms of of the
 * GNU General Public License Version 2 (the ``GPL'').
 *
 * Software distributed under the License is distributed
 * on an ``AS IS'' basis, WITHOUT WARRANTY OF ANY KIND, either
 * express or implied. See the GPL for the specific language
 * governing rights and limitations.
 *
 * You should have received a copy of the GPL along with this
 * program. If not, go to http://www.gnu.org/licenses/gpl.html
 * or write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *=END SONGBIRD GPL
 */
#include "sbDeviceLibraryMediaSyncSettings.h"

// Mozilla includes
#include <nsArrayUtils.h>
#include <nsAutoLock.h>
#include <nsAutoPtr.h>

// Songbird includes
#include <sbDeviceUtils.h>
#include <sbDeviceLibrarySyncSettings.h>
#include <sbIDeviceLibrary.h>
#include <sbIDeviceLibrarySyncSettings.h>
#include <sbILibrary.h>
#include <sbILibraryManager.h>
#include <sbLibraryUtils.h>
#include <sbHashtableUtils.h>

NS_IMPL_ISUPPORTS1(sbDeviceLibraryMediaSyncSettings, sbIDeviceLibraryMediaSyncSettings);

PLDHashOperator ArrayBuilder(nsISupports * aKey,
                             PRBool aData,
                             void* userArg)
{
  NS_ASSERTION(userArg, "ArrayBuilder passed a null arg");
  nsIMutableArray * array = static_cast<nsIMutableArray*>(userArg);
  if(aData) {
    nsresult rv = array->AppendElement(aKey, PR_FALSE);
    NS_ENSURE_SUCCESS(rv, PL_DHASH_STOP);
  }
  return PL_DHASH_NEXT;
}

PLDHashOperator ResetSelection(nsISupports * aKey,
                               PRBool & aData,
                               void* userArg)
{
  aData = PR_FALSE;
  return PL_DHASH_NEXT;
}

sbDeviceLibraryMediaSyncSettings *
sbDeviceLibraryMediaSyncSettings::New(
                                    sbDeviceLibrarySyncSettings * aSyncSettings,
                                    nsID const & aDeviceID,
                                    nsAString const & aDeviceLibraryGuid,
                                    PRUint32 aMediaType,
                                    PRLock * aLock)
{
  return new sbDeviceLibraryMediaSyncSettings(aSyncSettings,
                                              aDeviceID,
                                              aDeviceLibraryGuid,
                                              aMediaType,
                                              aLock);
}

sbDeviceLibraryMediaSyncSettings::sbDeviceLibraryMediaSyncSettings(
                                    sbDeviceLibrarySyncSettings * aSyncSettings,
                                    nsID const & aDeviceID,
                                    nsAString const & aDeviceLibraryGuid,
                                    PRUint32 aMediaType,
                                    PRLock * aLock) :
  mSyncMgmtType(sbIDeviceLibrarySyncSettings::SYNC_MODE_MANUAL),
  mMediaType(aMediaType),
  mDeviceID(aDeviceID),
  mDeviceLibraryGuid(aDeviceLibraryGuid),
  mLock(aLock),
  mSyncSettings(aSyncSettings)
{
  mPlaylistsSelection.Init();
}

sbDeviceLibraryMediaSyncSettings::~sbDeviceLibraryMediaSyncSettings()
{
}

struct PlaylistHashtableTraits
{
  typedef sbDeviceLibraryMediaSyncSettings::PlaylistSelection Hashtable;
  typedef nsISupports * KeyType;
  typedef PRBool DataType;
};

nsresult
sbDeviceLibraryMediaSyncSettings::Assign(
                                  sbDeviceLibraryMediaSyncSettings * aSettings)
{
  nsresult rv;

  mSyncMgmtType = aSettings->mSyncMgmtType;
  mMediaType = aSettings->mMediaType;
  mDeviceID = aSettings->mDeviceID;
  mDeviceLibraryGuid = aSettings->mDeviceLibraryGuid;

  rv = sbCopyHashtable<PlaylistHashtableTraits>(
                                               aSettings->mPlaylistsSelection,
                                               mPlaylistsSelection);
  NS_ENSURE_SUCCESS(rv, rv);
  mSyncFolder = aSettings->mSyncFolder;
  rv = aSettings->mSyncFromFolder->Clone(getter_AddRefs(mSyncFromFolder));
  NS_ENSURE_SUCCESS(rv, rv);

  mChanged = PR_FALSE;
  mLock = aSettings->mLock;

  return NS_OK;
}

void sbDeviceLibraryMediaSyncSettings::Changed()
{
  mChanged = true;
  mSyncSettings->Changed();
}

nsresult
sbDeviceLibraryMediaSyncSettings::CreateCopy(
                                  sbDeviceLibraryMediaSyncSettings ** aSettings)
{
  NS_ENSURE_ARG_POINTER(aSettings);
  nsresult rv;
  nsRefPtr<sbDeviceLibraryMediaSyncSettings> newSettings =
    sbDeviceLibraryMediaSyncSettings::New(mSyncSettings,
                                          mDeviceID,
                                          mDeviceLibraryGuid,
                                          mMediaType,
                                          mLock);

  newSettings->mSyncMgmtType = mSyncMgmtType;
  rv = sbCopyHashtable<PlaylistHashtableTraits>(
                                              mPlaylistsSelection,
                                              newSettings->mPlaylistsSelection);
  NS_ENSURE_SUCCESS(rv, rv);

  newSettings->mSyncFolder = mSyncFolder;
  if (mSyncFromFolder) {
    rv = mSyncFromFolder->Clone(getter_AddRefs(newSettings->mSyncFromFolder));
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    mSyncFromFolder = nsnull;
  }
  newSettings.forget(aSettings);

  return NS_OK;
}

NS_IMETHODIMP
sbDeviceLibraryMediaSyncSettings::GetMgmtType(PRUint32 *aSyncMgmtType)
{
  NS_ENSURE_ARG_POINTER(aSyncMgmtType);
  NS_ENSURE_TRUE(mLock, NS_ERROR_OUT_OF_MEMORY);
  nsAutoLock lock(mLock);
  *aSyncMgmtType = mSyncMgmtType;
  return NS_OK;
}

NS_IMETHODIMP
sbDeviceLibraryMediaSyncSettings::SetMgmtType(PRUint32 aSyncMgmtType)
{
  NS_ENSURE_TRUE(mLock, NS_ERROR_OUT_OF_MEMORY);
  nsAutoLock lock(mLock);
  mSyncMgmtType = aSyncMgmtType;
  Changed();
  return NS_OK;
}

NS_IMETHODIMP
sbDeviceLibraryMediaSyncSettings::GetSelectedPlaylists(
                                                 nsIArray ** aSelectedPlaylists)
{
  NS_ENSURE_ARG_POINTER(aSelectedPlaylists);
  NS_ENSURE_TRUE(mLock, NS_ERROR_OUT_OF_MEMORY);
  nsresult rv;

  nsCOMPtr<nsIMutableArray> selected =
    do_CreateInstance("@songbirdnest.com/moz/xpcom/threadsafe-array;1", &rv);

  nsAutoLock lock(mLock);
  mPlaylistsSelection.EnumerateRead(ArrayBuilder, selected.get());

  rv = CallQueryInterface(selected, aSelectedPlaylists);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbDeviceLibraryMediaSyncSettings::SetSelectedPlaylists(
                                                  nsIArray * aSelectedPlaylists)
{
  NS_ENSURE_ARG_POINTER(aSelectedPlaylists);
  NS_ENSURE_TRUE(mLock, NS_ERROR_OUT_OF_MEMORY);

  nsresult rv;

  nsAutoLock lock(mLock);

  mPlaylistsSelection.Enumerate(ResetSelection, nsnull);

  nsCOMPtr<nsISupports> medialist;
  PRUint32 length;
  rv = aSelectedPlaylists->GetLength(&length);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediaList> mediaList;
  for (PRUint32 index = 0; index < length; ++index) {
    mediaList = do_QueryElementAt(aSelectedPlaylists, index, &rv);
    NS_ENSURE_SUCCESS(rv, rv);
    mPlaylistsSelection.Put(mediaList, PR_TRUE);
  }
  Changed();
  return NS_OK;
}

NS_IMETHODIMP
sbDeviceLibraryMediaSyncSettings::SetPlaylistSelected(sbIMediaList *aPlaylist,
                                                      PRBool aSelected)
{
  NS_ENSURE_TRUE(mLock, NS_ERROR_OUT_OF_MEMORY);
  nsAutoLock lock(mLock);
  nsCOMPtr<nsISupports> supports = aPlaylist;
  mPlaylistsSelection.Put(supports, PR_TRUE);
  Changed();
  return NS_OK;
}

NS_IMETHODIMP
sbDeviceLibraryMediaSyncSettings::GetPlaylistSelected(sbIMediaList *aPlaylist,
                                                      PRBool * aSelected)
{
  NS_ENSURE_TRUE(mLock, NS_ERROR_OUT_OF_MEMORY);
  nsAutoLock lock(mLock);

  nsCOMPtr<nsISupports> supports = aPlaylist;
  PRBool exists = mPlaylistsSelection.Get(supports, aSelected);
  if (!exists) {
    *aSelected = PR_FALSE;
  }
  return NS_OK;
}

NS_IMETHODIMP
sbDeviceLibraryMediaSyncSettings::ClearSelectedPlaylists()
{
  NS_ENSURE_TRUE(mLock, NS_ERROR_OUT_OF_MEMORY);
  nsAutoLock lock(mLock);

  mPlaylistsSelection.Enumerate(ResetSelection, nsnull);
  Changed();

  return NS_OK;
}

NS_IMETHODIMP
sbDeviceLibraryMediaSyncSettings::GetSyncFolder(nsAString & aSyncFolder)
{
  NS_ENSURE_TRUE(mLock, NS_ERROR_OUT_OF_MEMORY);
  nsAutoLock lock(mLock);

  aSyncFolder = mSyncFolder;

  return NS_OK;
}

NS_IMETHODIMP
sbDeviceLibraryMediaSyncSettings::SetSyncFolder(const nsAString & aSyncFolder)
{
  NS_ENSURE_TRUE(mLock, NS_ERROR_OUT_OF_MEMORY);
  nsAutoLock lock(mLock);

  mSyncFolder = aSyncFolder;
  Changed();

  return NS_OK;
}

NS_IMETHODIMP
sbDeviceLibraryMediaSyncSettings::GetSyncFromFolder(nsIFile ** aSyncFromFolder)
{
  NS_ENSURE_TRUE(mLock, NS_ERROR_OUT_OF_MEMORY);
  nsAutoLock lock(mLock);

  if (!mSyncFromFolder) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  nsresult rv = mSyncFromFolder->Clone(aSyncFromFolder);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

NS_IMETHODIMP
sbDeviceLibraryMediaSyncSettings::SetSyncFromFolder(
                                              nsIFile * aSyncFromFolder)
{
  NS_ENSURE_TRUE(mLock, NS_ERROR_OUT_OF_MEMORY);
  nsAutoLock lock(mLock);

  nsresult rv = aSyncFromFolder->Clone(getter_AddRefs(mSyncFromFolder));
  NS_ENSURE_SUCCESS(rv, rv);
  Changed();

  return NS_OK;
}

nsresult
sbDeviceLibraryMediaSyncSettings::GetSyncPlaylistsNoLock(
                                              nsIArray ** aSyncPlaylists,
                                              sbIDeviceLibrary * aDeviceLibrary)
{
  NS_ENSURE_ARG_POINTER(aSyncPlaylists);

  nsresult rv;

  // Conver the media type to the content type
  PRUint32 contentType;
  switch (mMediaType) {
    case sbIDeviceLibrary::MEDIATYPE_AUDIO:
      contentType = sbIMediaList::CONTENTTYPE_AUDIO;
      break;
    case sbIDeviceLibrary::MEDIATYPE_VIDEO:
      contentType = sbIMediaList::CONTENTTYPE_VIDEO;
      break;
    default:
      return NS_ERROR_NOT_AVAILABLE;
  }
  nsCOMPtr<sbIDeviceLibrary> library;
  if (aDeviceLibrary) {
    library = aDeviceLibrary;
  }
  else {
    rv = sbDeviceUtils::GetDeviceLibrary(mDeviceLibraryGuid,
                                         &mDeviceID,
                                         getter_AddRefs(library));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = sbLibraryUtils::GetMediaListByContentType(library,
                                                 contentType,
                                                 aSyncPlaylists);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbDeviceLibraryMediaSyncSettings::GetSyncPlaylists(nsIArray ** aSyncPlaylists)
{
  NS_ENSURE_TRUE(mLock, NS_ERROR_OUT_OF_MEMORY);

  nsAutoLock lock(mLock);
  return GetSyncPlaylistsNoLock(aSyncPlaylists, nsnull);
}

NS_IMETHODIMP
sbDeviceLibraryMediaSyncSettings::AddPlaylistListener(sbIDeviceLibraryPlaylistListener *aListener)
{
  NS_ENSURE_TRUE(mLock, NS_ERROR_OUT_OF_MEMORY);
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
sbDeviceLibraryMediaSyncSettings::RemovePlaylistListener(sbIDeviceLibraryPlaylistListener *aListener)
{
  NS_ENSURE_TRUE(mLock, NS_ERROR_OUT_OF_MEMORY);
    return NS_ERROR_NOT_IMPLEMENTED;
}