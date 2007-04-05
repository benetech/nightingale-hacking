/*
//
// BEGIN SONGBIRD GPL
//
// This file is part of the Songbird web player.
//
// Copyright(c) 2005-2007 POTI, Inc.
// http://songbirdnest.com
//
// This file may be licensed under the terms of of the
// GNU General Public License Version 2 (the "GPL").
//
// Software distributed under the License is distributed
// on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either
// express or implied. See the GPL for the specific language
// governing rights and limitations.
//
// You should have received a copy of the GPL along with this
// program. If not, go to http://www.gnu.org/licenses/gpl.html
// or write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
// END SONGBIRD GPL
//
*/

#include "sbLocalDatabasePropertyCache.h"

#include <DatabaseQuery.h>
#include <nsCOMArray.h>
#include <nsIURI.h>
#include <nsMemory.h>
#include <nsStringEnumerator.h>
#include <nsUnicharUtils.h>
#include <prlog.h>
#include <sbSQLBuilderCID.h>
#include <sbDatabaseResultStringEnumerator.h>
#include <nsAutoLock.h>

#define MAX_IN_LENGTH 5000

/*
 * To log this module, set the following environment variable:
 *   NSPR_LOG_MODULES=sbLocalDatabasePropertyCache:5
 */
#ifdef PR_LOGGING
static PRLogModuleInfo *gLocalDatabasePropertyCacheLog = nsnull;
#define TRACE(args) if (gLocalDatabasePropertyCacheLog) PR_LOG(gLocalDatabasePropertyCacheLog, PR_LOG_DEBUG, args)
#define LOG(args)   if (gLocalDatabasePropertyCacheLog) PR_LOG(gLocalDatabasePropertyCacheLog, PR_LOG_WARN, args)
#else
#define TRACE(args) /* nothing */
#define LOG(args)   /* nothing */
#endif

NS_IMPL_ISUPPORTS1(sbLocalDatabasePropertyCache, sbILocalDatabasePropertyCache)

struct sbStaticProperty {
  const char* mName;
  const char* mColumn;
  PRUint32    mID;
};

static sbStaticProperty kStaticProperties[] = {
  {
    "http://songbirdnest.com/data/1.0#created",
    "created",
    PR_UINT32_MAX,
  },
  {
    "http://songbirdnest.com/data/1.0#updated",
    "updated",
    PR_UINT32_MAX - 1,
  },
  {
    "http://songbirdnest.com/data/1.0#contentUrl",
    "content_url",
    PR_UINT32_MAX - 2,
  },
  {
    "http://songbirdnest.com/data/1.0#contentMimeType",
    "content_mime_type",
    PR_UINT32_MAX - 3,
  },
  {
    "http://songbirdnest.com/data/1.0#contentLength",
    "content_length",
    PR_UINT32_MAX - 4,
  }
};

sbLocalDatabasePropertyCache::sbLocalDatabasePropertyCache() 
: mInitalized(PR_FALSE),
  mWritePending(PR_FALSE)
{
  mNumStaticProperties = sizeof(kStaticProperties) / sizeof(kStaticProperties[0]);

#ifdef PR_LOGGING
  if (!gLocalDatabasePropertyCacheLog) {
    gLocalDatabasePropertyCacheLog = PR_NewLogModule("sbLocalDatabasePropertyCache");
  }
#endif
}

sbLocalDatabasePropertyCache::~sbLocalDatabasePropertyCache()
{
}

NS_IMETHODIMP
sbLocalDatabasePropertyCache::GetWritePending(PRBool *aWritePending)
{
  NS_ASSERTION(mInitalized, "You didn't initalize!");
  NS_ENSURE_ARG_POINTER(aWritePending);
  *aWritePending = mWritePending;

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabasePropertyCache::Init(const nsAString& aDatabaseGuid,
                                   nsIURI* aDatabaseLocation)
{
  NS_ASSERTION(!mInitalized, "Already initalized!");

  nsresult rv;

  mDatabaseGUID = aDatabaseGuid;
  mDatabaseLocation = aDatabaseLocation;

  /*
   * Simple select from properties table with an in list of guids
   */
  mPropertiesSelect = do_CreateInstance(SB_SQLBUILDER_SELECT_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertiesSelect->SetBaseTableName(NS_LITERAL_STRING("resource_properties"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertiesSelect->AddColumn(EmptyString(), NS_LITERAL_STRING("guid"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertiesSelect->AddColumn(EmptyString(), NS_LITERAL_STRING("property_id"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertiesSelect->AddColumn(EmptyString(), NS_LITERAL_STRING("obj"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertiesSelect->CreateMatchCriterionIn(EmptyString(),
                                                 NS_LITERAL_STRING("guid"),
                                                 getter_AddRefs(mPropertiesInCriterion));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertiesSelect->AddCriterion(mPropertiesInCriterion);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertiesSelect->AddOrder(EmptyString(),
                                   NS_LITERAL_STRING("guid"),
                                   PR_TRUE);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertiesSelect->AddOrder(EmptyString(),
                                   NS_LITERAL_STRING("property_id"),
                                   PR_TRUE);
  NS_ENSURE_SUCCESS(rv, rv);

  /*
   * Create simple media_items query with in list of guids
   */
  mMediaItemsSelect = do_CreateInstance(SB_SQLBUILDER_SELECT_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mMediaItemsSelect->SetBaseTableName(NS_LITERAL_STRING("media_items"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mMediaItemsSelect->AddColumn(EmptyString(), NS_LITERAL_STRING("guid"));
  NS_ENSURE_SUCCESS(rv, rv);

  for (PRUint32 i = 0; i < mNumStaticProperties; i++) {
    rv = mMediaItemsSelect->AddColumn(EmptyString(),
                                      NS_ConvertUTF8toUTF16(kStaticProperties[i].mColumn));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = mMediaItemsSelect->CreateMatchCriterionIn(EmptyString(),
                                                 NS_LITERAL_STRING("guid"),
                                                 getter_AddRefs(mMediaItemsInCriterion));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mMediaItemsSelect->AddCriterion(mMediaItemsInCriterion);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mMediaItemsSelect->AddOrder(EmptyString(),
                                   NS_LITERAL_STRING("guid"),
                                   PR_TRUE);
  NS_ENSURE_SUCCESS(rv, rv);

  /*
   * Create property insert query builder
   *
   * INSERT INTO resource_properties (guid, property_id, obj, obj_sortable) VALUES (?, ?, ?, ?)
   */
  mPropertiesInsert = do_CreateInstance(SB_SQLBUILDER_INSERT_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertiesInsert->SetIntoTableName(NS_LITERAL_STRING("resource_properties"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertiesInsert->AddColumn(NS_LITERAL_STRING("guid"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertiesInsert->AddColumn(NS_LITERAL_STRING("property_id"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertiesInsert->AddColumn(NS_LITERAL_STRING("obj"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertiesInsert->AddColumn(NS_LITERAL_STRING("obj_sortable"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertiesInsert->AddValueParameter();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertiesInsert->AddValueParameter();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertiesInsert->AddValueParameter();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertiesInsert->AddValueParameter();
  NS_ENSURE_SUCCESS(rv, rv);

  /*
   * Create property update query builder.
   *
   * UPDATE resource_properties SET obj = ?, obj_sortable = ? WHERE guid = ? AND property_id = ?
   */
  mPropertiesUpdate = do_CreateInstance(SB_SQLBUILDER_UPDATE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertiesUpdate->SetTableName(NS_LITERAL_STRING("resource_properties"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertiesUpdate->AddAssignmentParameter(NS_LITERAL_STRING("obj"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertiesUpdate->AddAssignmentParameter(NS_LITERAL_STRING("obj_sortable"));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbISQLBuilderCriterion> criterionLeft;
  rv = mPropertiesUpdate->CreateMatchCriterionParameter(EmptyString(), 
                                                        NS_LITERAL_STRING("guid"),
                                                        sbISQLBuilder::MATCH_EQUALS,
                                                        getter_AddRefs(criterionLeft));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbISQLBuilderCriterion> criterionRight;
  rv = mPropertiesUpdate->CreateMatchCriterionParameter(EmptyString(),
                                                        NS_LITERAL_STRING("property_id"),
                                                        sbISQLBuilder::MATCH_EQUALS,
                                                        getter_AddRefs(criterionRight));

  nsCOMPtr<sbISQLBuilderCriterion> criterionAnd;
  rv = mPropertiesUpdate->CreateAndCriterion(criterionLeft, criterionRight, getter_AddRefs(criterionAnd));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertiesUpdate->AddCriterion(criterionAnd);
  NS_ENSURE_SUCCESS(rv, rv);

  /*
   * Create media item property update query builder.
   * This one can't be prepared in advance.
   */
  mMediaItemsUpdate = do_CreateInstance(SB_SQLBUILDER_UPDATE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  /*
   * Create query used to verify if we need to insert or update a property
   */
  mPropertyInsertSelect = do_CreateInstance(SB_SQLBUILDER_SELECT_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertyInsertSelect->SetBaseTableName(NS_LITERAL_STRING("resource_properties"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertyInsertSelect->AddColumn(EmptyString(), NS_LITERAL_STRING("guid"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertyInsertSelect->CreateMatchCriterionParameter(EmptyString(), 
                                                            NS_LITERAL_STRING("guid"),
                                                            sbISQLBuilder::MATCH_EQUALS,
                                                            getter_AddRefs(criterionLeft));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertyInsertSelect->CreateMatchCriterionParameter(EmptyString(),
                                                            NS_LITERAL_STRING("property_id"),
                                                            sbISQLBuilder::MATCH_EQUALS,
                                                            getter_AddRefs(criterionRight));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertyInsertSelect->CreateAndCriterion(criterionLeft, criterionRight, getter_AddRefs(criterionAnd));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropertyInsertSelect->AddCriterion(criterionAnd);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = LoadProperties();
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool success = mCache.Init(1000);
  NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);

  success = mDirty.Init(1000);
  NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);

  mInitalized = PR_TRUE;

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabasePropertyCache::CacheProperties(const PRUnichar **aGUIDArray,
                                              PRUint32 aGUIDArrayCount)
{
  NS_ASSERTION(mInitalized, "You didn't initalize!");
  nsresult rv;

  /*
   * First, collect all the guids that are not cached
   */
  nsTArray<nsString> misses;
  for (PRUint32 i = 0; i < aGUIDArrayCount; i++) {
    nsAutoString guid(aGUIDArray[i]);
    if (!mCache.Get(guid, nsnull)) {
      nsString* success = misses.AppendElement(guid);
      NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);
    }
  }

  /*
   * Look up and cache each of the misses
   */
  PRUint32 numMisses = misses.Length();
  if (numMisses > 0) {
    PRUint32 inNum = 0;
    for (PRUint32 j = 0; j < numMisses; j++) {

      /*
       * Add each guid to the query and execute the query when we've added
       * MAX_IN_LENGTH of them (or when we are on the last one)
       */
      rv = mPropertiesInCriterion->AddString(misses[j]);
      NS_ENSURE_SUCCESS(rv, rv);

      if (inNum > MAX_IN_LENGTH || j + 1 == numMisses) {
        PRInt32 dbOk;

        nsAutoString sql;
        rv = mPropertiesSelect->ToString(sql);
        NS_ENSURE_SUCCESS(rv, rv);

        nsCOMPtr<sbIDatabaseQuery> query;
        rv = MakeQuery(sql, getter_AddRefs(query));
        NS_ENSURE_SUCCESS(rv, rv);

        rv = query->Execute(&dbOk);
        NS_ENSURE_SUCCESS(rv, rv);
        NS_ENSURE_SUCCESS(dbOk, dbOk);

        rv = query->WaitForCompletion(&dbOk);
        NS_ENSURE_SUCCESS(rv, rv);
        NS_ENSURE_SUCCESS(dbOk, dbOk);

        nsCOMPtr<sbIDatabaseResult> result;
        rv = query->GetResultObject(getter_AddRefs(result));
        NS_ENSURE_SUCCESS(rv, rv);

        PRUint32 rowCount;
        rv = result->GetRowCount(&rowCount);
        NS_ENSURE_SUCCESS(rv, rv);

        nsAutoString lastGUID;
        nsCOMPtr<sbILocalDatabaseResourcePropertyBag> bag;
        for (PRUint32 row = 0; row < rowCount; row++) {
          PRUnichar* guid;
          rv = result->GetRowCellPtr(row, 0, &guid);
          NS_ENSURE_SUCCESS(rv, rv);

          /*
           * If this is the first row result or we've encountered a new
           * guid, create a new property bag and add it to the cache
           */
          if (row == 0 || !lastGUID.Equals(guid)) {
            lastGUID = guid;
            nsAutoPtr<sbLocalDatabaseResourcePropertyBag> newBag
              (new sbLocalDatabaseResourcePropertyBag(this, lastGUID));
            NS_ENSURE_TRUE(newBag, NS_ERROR_OUT_OF_MEMORY);

            rv = newBag->Init();
            NS_ENSURE_SUCCESS(rv, rv);

            bag = newBag.forget();
            PRBool success = mCache.Put(lastGUID, bag);
            NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);
          }

          /*
           * Add each property / object pair to the current bag
           */
          nsAutoString propertyIDStr;
          rv = result->GetRowCell(row, 1, propertyIDStr);
          NS_ENSURE_SUCCESS(rv, rv);

          PRUint32 propertyID = propertyIDStr.ToInteger(&rv);
          NS_ENSURE_SUCCESS(rv, rv);

          nsAutoString obj;
          rv = result->GetRowCell(row, 2, obj);
          NS_ENSURE_SUCCESS(rv, rv);

          rv = NS_STATIC_CAST(sbLocalDatabaseResourcePropertyBag*, bag.get())
                                ->PutValue(propertyID, obj);
          NS_ENSURE_SUCCESS(rv, rv);
        }

        mPropertiesInCriterion->Clear();
        NS_ENSURE_SUCCESS(rv, rv);
      }

    }

    /*
     * Do the same thing for top level properties
     */
    inNum = 0;
    for (PRUint32 j = 0; j < numMisses; j++) {

      rv = mMediaItemsInCriterion->AddString(misses[j]);
      NS_ENSURE_SUCCESS(rv, rv);

      if (inNum > MAX_IN_LENGTH || j + 1 == numMisses) {
        PRInt32 dbOk;

        nsAutoString sql;
        rv = mMediaItemsSelect->ToString(sql);
        NS_ENSURE_SUCCESS(rv, rv);

        nsCOMPtr<sbIDatabaseQuery> query;
        rv = MakeQuery(sql, getter_AddRefs(query));
        NS_ENSURE_SUCCESS(rv, rv);

        rv = query->Execute(&dbOk);
        NS_ENSURE_SUCCESS(rv, rv);
        NS_ENSURE_SUCCESS(dbOk, dbOk);

        rv = query->WaitForCompletion(&dbOk);
        NS_ENSURE_SUCCESS(rv, rv);
        NS_ENSURE_SUCCESS(dbOk, dbOk);

        nsCOMPtr<sbIDatabaseResult> result;
        rv = query->GetResultObject(getter_AddRefs(result));
        NS_ENSURE_SUCCESS(rv, rv);

        PRUint32 rowCount;
        rv = result->GetRowCount(&rowCount);
        NS_ENSURE_SUCCESS(rv, rv);

        sbILocalDatabaseResourcePropertyBag* bag;
        for (PRUint32 row = 0; row < rowCount; row++) {
          nsAutoString guid;
          rv = result->GetRowCell(row, 0, guid);
          NS_ENSURE_SUCCESS(rv, rv);

          if (!mCache.Get(guid, &bag)) {
            nsAutoPtr<sbLocalDatabaseResourcePropertyBag> newBag
              (new sbLocalDatabaseResourcePropertyBag(this, guid));
            NS_ENSURE_TRUE(newBag, NS_ERROR_OUT_OF_MEMORY);

            rv = newBag->Init();
            NS_ENSURE_SUCCESS(rv, rv);

            bag = newBag.forget();
            PRBool success = mCache.Put(guid, bag);
            NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);
          }

          for (PRUint32 i = 0; i < mNumStaticProperties; i++) {
            nsAutoString value;
            rv = result->GetRowCell(row, i + 1, value);
            NS_ENSURE_SUCCESS(rv, rv);

            rv = NS_STATIC_CAST(sbLocalDatabaseResourcePropertyBag*, bag)
                                  ->PutValue(kStaticProperties[i].mID, value);
            NS_ENSURE_SUCCESS(rv, rv);
          }

        }

        mMediaItemsInCriterion->Clear();
        NS_ENSURE_SUCCESS(rv, rv);
      }

    }

  }

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabasePropertyCache::GetProperties(const PRUnichar **aGUIDArray,
                                            PRUint32 aGUIDArrayCount,
                                            PRUint32 *aPropertyArrayCount,
                                            sbILocalDatabaseResourcePropertyBag ***aPropertyArray)
{
  NS_ASSERTION(mInitalized, "You didn't initalize!");
  nsresult rv;

  NS_ENSURE_ARG_POINTER(aPropertyArrayCount);
  NS_ENSURE_ARG_POINTER(aPropertyArray);

  rv = CacheProperties(aGUIDArray, aGUIDArrayCount);
  NS_ENSURE_SUCCESS(rv, rv);

  /*
   * Build the output array using cache lookups
   */
  sbILocalDatabaseResourcePropertyBag **propertyBagArray = nsnull;

  *aPropertyArrayCount = aGUIDArrayCount;
  if (aGUIDArrayCount > 0) {

    propertyBagArray = (sbILocalDatabaseResourcePropertyBag **)
      nsMemory::Alloc((sizeof (sbILocalDatabaseResourcePropertyBag *)) * *aPropertyArrayCount);

    for (PRUint32 i = 0; i < aGUIDArrayCount; i++) {
      nsAutoString guid(aGUIDArray[i]);
      sbILocalDatabaseResourcePropertyBag* bag;
      if (mCache.Get(guid, &bag)) {
        propertyBagArray[i] = bag;
        NS_ADDREF(propertyBagArray[i]);
      }
      else {
        propertyBagArray[i] = nsnull;
      }
    }
  }
  else {
    *propertyBagArray = nsnull;
  }

  *aPropertyArray = propertyBagArray;

  return NS_OK;
}


NS_IMETHODIMP 
sbLocalDatabasePropertyCache::SetProperties(const PRUnichar **aGUIDArray, 
                                            PRUint32 aGUIDArrayCount, 
                                            sbILocalDatabaseResourcePropertyBag **aPropertyArray, 
                                            PRUint32 aPropertyArrayCount, 
                                            PRBool aWriteThroughNow)
{
  NS_ASSERTION(mInitalized, "You didn't initalize!");
  NS_ENSURE_ARG_POINTER(aGUIDArray);
  NS_ENSURE_ARG_POINTER(aPropertyArray);
  NS_ENSURE_TRUE(aGUIDArrayCount == aPropertyArrayCount, NS_ERROR_INVALID_ARG);

  nsresult rv = NS_OK;

  for(PRUint32 i = 0; i < aGUIDArrayCount; i++) {
    nsAutoString guid(aGUIDArray[i]);
    sbILocalDatabaseResourcePropertyBag* bag = nsnull;

    if(mCache.Get(guid, &bag) && bag) {
      nsCOMPtr<nsIStringEnumerator> names;
      rv = aPropertyArray[i]->GetNames(getter_AddRefs(names));
      NS_ENSURE_SUCCESS(rv, rv);

      PRBool hasMore = PR_FALSE;
      nsAutoString name, value;

      while(NS_SUCCEEDED(names->HasMore(&hasMore)) && hasMore) {
        rv = names->GetNext(name);
        NS_ENSURE_SUCCESS(rv, rv);

        rv = aPropertyArray[i]->GetProperty(name, value);
        NS_ENSURE_SUCCESS(rv, rv);

        rv = NS_STATIC_CAST(sbLocalDatabaseResourcePropertyBag*, bag)
          ->SetProperty(name, value);
      }

      mDirty.PutEntry(guid);
    }
  }

  if(aWriteThroughNow) {
    rv = Write();
  }

  return rv;
}

PR_STATIC_CALLBACK(PLDHashOperator)
EnumDirtyGuids(nsStringHashKey *aKey, void *aClosure)
{
  nsTArray<nsString> *dirtyGuids = NS_STATIC_CAST(nsTArray<nsString> *, aClosure);
  dirtyGuids->AppendElement(aKey->GetKey());
  return PL_DHASH_NEXT;
}

PR_STATIC_CALLBACK(PLDHashOperator)
EnumDirtyProps(nsUint32HashKey *aKey, void *aClosure)
{
  nsTArray<PRUint32> *dirtyProps = NS_STATIC_CAST(nsTArray<PRUint32> *, aClosure);
  dirtyProps->AppendElement(aKey->GetKey());
  return PL_DHASH_NEXT;
}

NS_IMETHODIMP 
sbLocalDatabasePropertyCache::Write()
{
  NS_ASSERTION(mInitalized, "You didn't initalize!");

  nsresult rv = NS_OK;
  nsTArray<nsString> dirtyGuids;

  nsCOMPtr<sbIDatabaseQuery> query;
  rv = MakeQuery(NS_LITERAL_STRING("begin"), getter_AddRefs(query));
  NS_ENSURE_SUCCESS(rv, rv);

  //Enumerate dirty GUIDs
  PRUint32 dirtyGuidCount = mDirty.EnumerateEntries(EnumDirtyGuids, (void *) &dirtyGuids);

  //For each GUID, there's a property bag that needs to be processed as well.
  for(PRUint32 i = 0; i < dirtyGuidCount; i++) {
    sbILocalDatabaseResourcePropertyBag* bag;
    if (mCache.Get(dirtyGuids[i], &bag)) {
      nsTArray<PRUint32> dirtyProps;
      sbLocalDatabaseResourcePropertyBag* bagLocal = 
        NS_STATIC_CAST(sbLocalDatabaseResourcePropertyBag *, bag);

      PRUint32 dirtyPropsCount = 0;
      rv = bagLocal->EnumerateDirty(EnumDirtyProps, (void *) &dirtyProps, &dirtyPropsCount);
      NS_ENSURE_SUCCESS(rv, rv);

      //Enumerate dirty properties for this GUID.
      nsAutoString value;
      for(PRUint32 j = 0; j < dirtyPropsCount; j++) {
        nsAutoString sql;
        bagLocal->GetPropertyByID(dirtyProps[j], value);
        
        //Top level properties need to be treated differently, so check for them.
        if(IsTopLevelProperty(dirtyProps[j])) {

          rv = mMediaItemsUpdate->SetTableName(NS_LITERAL_STRING("media_items"));
          NS_ENSURE_SUCCESS(rv, rv);

          nsAutoString column;
          GetColumnForPropertyID(dirtyProps[j], column);

          rv = mMediaItemsUpdate->AddAssignmentString(column, value);
          NS_ENSURE_SUCCESS(rv, rv);

          nsCOMPtr<sbISQLBuilderCriterion> criterion;
          rv = mMediaItemsUpdate->CreateMatchCriterionParameter(dirtyGuids[i], 
                                                                NS_LITERAL_STRING("guid"),
                                                                sbISQLBuilder::MATCH_EQUALS,
                                                                getter_AddRefs(criterion));
          NS_ENSURE_SUCCESS(rv, rv);

          rv = mMediaItemsUpdate->AddCriterion(criterion);
          NS_ENSURE_SUCCESS(rv, rv);
          
          rv = mMediaItemsUpdate->ToString(sql);
          NS_ENSURE_SUCCESS(rv, rv);

          rv = query->AddQuery(sql);
          NS_ENSURE_SUCCESS(rv, rv);
        }
        else { //Regular properties all go in the same spot.
          nsAutoString sortable;

          //XXX: Replace with PropertyManager makeSortable.
          ToLowerCase(value, sortable);
          CompressWhitespace(sortable);

          //Check if we need to insert or update the property.
          PRBool bNeedInsert = PR_FALSE;
          PropertyRequiresInsert(dirtyGuids[i], dirtyProps[j], &bNeedInsert);
          if(bNeedInsert) {
            rv = mPropertiesInsert->ToString(sql);
            NS_ENSURE_SUCCESS(rv, rv);

            rv = query->AddQuery(sql);
            NS_ENSURE_SUCCESS(rv, rv);

            rv = query->BindStringParameter(0, dirtyGuids[i]);
            NS_ENSURE_SUCCESS(rv, rv);

            rv = query->BindInt32Parameter(1, dirtyProps[j]);
            NS_ENSURE_SUCCESS(rv, rv);

            rv = query->BindStringParameter(2, value);
            NS_ENSURE_SUCCESS(rv, rv);

            rv = query->BindStringParameter(3, sortable);
            NS_ENSURE_SUCCESS(rv, rv);
          } 
          else {
            rv = mPropertiesUpdate->ToString(sql);
            NS_ENSURE_SUCCESS(rv, rv);

            rv = query->AddQuery(sql);
            NS_ENSURE_SUCCESS(rv, rv);

            rv = query->BindStringParameter(0, value);
            NS_ENSURE_SUCCESS(rv, rv);

            rv = query->BindStringParameter(1, sortable);
            NS_ENSURE_SUCCESS(rv, rv);

            rv = query->BindStringParameter(2, dirtyGuids[i]);
            NS_ENSURE_SUCCESS(rv, rv);

            rv = query->BindInt32Parameter(3, dirtyProps[j]);
            NS_ENSURE_SUCCESS(rv, rv);
          }
        }
      }
    }
  }

  if(dirtyGuidCount)
  {
    PRInt32 dbOk;

    rv = query->AddQuery(NS_LITERAL_STRING("commit"));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = query->Execute(&dbOk);
    NS_ENSURE_SUCCESS(rv, rv);
    NS_ENSURE_SUCCESS(dbOk, dbOk);

    rv = query->WaitForCompletion(&dbOk);
    NS_ENSURE_SUCCESS(rv, rv);
    NS_ENSURE_SUCCESS(dbOk, dbOk);

    for(PRUint32 i = 0; i < dirtyGuidCount; i++) {
      sbILocalDatabaseResourcePropertyBag* bag;
      if (mCache.Get(dirtyGuids[i], &bag)) {
        sbLocalDatabaseResourcePropertyBag* bagLocal = 
          NS_STATIC_CAST(sbLocalDatabaseResourcePropertyBag *, bag);
        bagLocal->SetDirty(PR_FALSE);
      }
    }
    
    //Clear dirty guid hastable.
    mDirty.Clear();
  }

  return rv;
}

nsresult
sbLocalDatabasePropertyCache::MakeQuery(const nsAString& aSql,
                                        sbIDatabaseQuery** _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  LOG(("MakeQuery: %s", NS_ConvertUTF16toUTF8(aSql).get()));

  nsresult rv;

  nsCOMPtr<sbIDatabaseQuery> query =
    do_CreateInstance(SONGBIRD_DATABASEQUERY_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = query->SetDatabaseGUID(mDatabaseGUID);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mDatabaseLocation) {
    rv = query->SetDatabaseLocation(mDatabaseLocation);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = query->SetAsyncQuery(PR_FALSE);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = query->AddQuery(aSql);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ADDREF(*_retval = query);
  return NS_OK;
}

nsresult
sbLocalDatabasePropertyCache::LoadProperties()
{
  nsresult rv;
  PRInt32 dbOk;

  if (!mPropertyNameToID.IsInitialized()) {
    PRBool success = mPropertyNameToID.Init(100);
    NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);
  }
  else {
    mPropertyNameToID.Clear();
  }

  if (!mPropertyIDToName.IsInitialized()) {
    PRBool success = mPropertyIDToName.Init(100);
    NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);
  }
  else {
    mPropertyIDToName.Clear();
  }

  nsCOMPtr<sbISQLSelectBuilder> builder =
    do_CreateInstance(SB_SQLBUILDER_SELECT_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = builder->SetBaseTableName(NS_LITERAL_STRING("properties"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = builder->AddColumn(EmptyString(), NS_LITERAL_STRING("property_id"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = builder->AddColumn(EmptyString(), NS_LITERAL_STRING("property_name"));
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString sql;
  rv = builder->ToString(sql);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIDatabaseQuery> query;
  rv = MakeQuery(sql, getter_AddRefs(query));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = query->Execute(&dbOk);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_SUCCESS(dbOk, dbOk);

  rv = query->WaitForCompletion(&dbOk);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_SUCCESS(dbOk, dbOk);

  nsCOMPtr<sbIDatabaseResult> result;
  rv = query->GetResultObject(getter_AddRefs(result));
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 rowCount;
  rv = result->GetRowCount(&rowCount);
  NS_ENSURE_SUCCESS(rv, rv);

  for (PRUint32 i = 0; i < rowCount; i++) {
    nsAutoString propertyIDStr;
    rv = result->GetRowCell(i, 0, propertyIDStr);
    NS_ENSURE_SUCCESS(rv, rv);

    PRUint32 propertyID = propertyIDStr.ToInteger(&rv);
    NS_ENSURE_SUCCESS(rv, rv);

    nsAutoString propertyName;
    rv = result->GetRowCell(i, 1, propertyName);
    NS_ENSURE_SUCCESS(rv, rv);

    nsAutoPtr<nsString> propertyNamePtr(new nsString(propertyName));
    PRBool success = mPropertyIDToName.Put(propertyID, propertyNamePtr);
    NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);
    propertyNamePtr.forget();
    TRACE(("Added %d => %s to property name cache", propertyID,
           NS_ConvertUTF16toUTF8(propertyName).get()));
 
    success = mPropertyNameToID.Put(propertyName, propertyID);
    NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);

  }

  /*
   * Add top level properties
   */
  for (PRUint32 i = 0; i < mNumStaticProperties; i++) {

    /*
     * Convert the char* constants to nsString
     */
    nsCAutoString propertyNameCString(kStaticProperties[i].mName);
    nsAutoString propertyName(NS_ConvertUTF8toUTF16(propertyNameCString).get());

    nsAutoPtr<nsString> propertyNamePtr(new nsString(propertyName));
    PRBool success = mPropertyIDToName.Put(kStaticProperties[i].mID, propertyNamePtr);
    NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);
    propertyNamePtr.forget();

    success = mPropertyNameToID.Put(propertyName, kStaticProperties[i].mID);
    NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);
  }

  return NS_OK;
}

nsresult
sbLocalDatabasePropertyCache::AddDirtyGUID(const nsAString &aGuid)
{
  nsAutoString guid(aGuid);
  mDirty.PutEntry(guid);

  return NS_OK;
}

PRUint32
sbLocalDatabasePropertyCache::GetPropertyID(const nsAString& aPropertyName)
{
  PRUint32 retval;
  if (!mPropertyNameToID.Get(aPropertyName, &retval)) {
    retval = 0;
  }
  return retval;
}

PRBool
sbLocalDatabasePropertyCache::GetPropertyName(PRUint32 aPropertyID,
                                              nsAString& aPropertyName)
{
  nsString *propertyName;
  if (mPropertyIDToName.Get(aPropertyID, &propertyName)) {
    aPropertyName = *propertyName;
    return PR_TRUE;
  }
  return PR_FALSE;
}

PRBool 
sbLocalDatabasePropertyCache::IsTopLevelProperty(PRUint32 aPropertyID)
{
  //XXX: This should use the property manager when it becomes available.
  PRUint32 numTopLevelProps = sizeof(kStaticProperties) / sizeof(sbStaticProperty); 
  for(PRUint32 i = 0; i < numTopLevelProps; i++) {
    if(kStaticProperties[i].mID == aPropertyID)
      return PR_TRUE;
  }
  return PR_FALSE;
}

nsresult
sbLocalDatabasePropertyCache::PropertyRequiresInsert(const nsAString &aGuid, PRUint32 aPropertyID, PRBool *aInsert)
{
  NS_ENSURE_ARG_POINTER(aInsert);
  *aInsert = PR_TRUE;

  nsAutoString sql;
  nsresult rv = mPropertyInsertSelect->ToString(sql);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIDatabaseQuery> query;
  rv = MakeQuery(sql, getter_AddRefs(query));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = query->BindStringParameter(0, aGuid);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = query->BindInt32Parameter(1, aPropertyID);
  NS_ENSURE_SUCCESS(rv, rv);

  PRInt32 dbOk;
  rv = query->Execute(&dbOk);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_SUCCESS(dbOk, dbOk);

  rv = query->WaitForCompletion(&dbOk);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_SUCCESS(dbOk, dbOk);

  nsCOMPtr<sbIDatabaseResult> result;
  rv = query->GetResultObject(getter_AddRefs(result));
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 rowCount;
  rv = result->GetRowCount(&rowCount);
  NS_ENSURE_SUCCESS(rv, rv);

  if(rowCount > 0) {
    *aInsert = PR_FALSE;
  }

  return NS_OK;
}

void
sbLocalDatabasePropertyCache::GetColumnForPropertyID(PRUint32 aPropertyID, 
                                                     nsAString &aColumn)
{
  //XXX: This needs to use the property manager when it becomes available.
  PRUint32 numTopLevelProps = sizeof(kStaticProperties) / sizeof(sbStaticProperty); 
  for(PRUint32 i = 0; i < numTopLevelProps; i++) {
    if(kStaticProperties[i].mID == aPropertyID) {
      NS_ConvertUTF8toUTF16 column(kStaticProperties[i].mColumn);
      aColumn = column;
      return;
    }
  }
  return;
}

// sbILocalDatabaseResourcePropertyBag
NS_IMPL_THREADSAFE_ISUPPORTS1(sbLocalDatabaseResourcePropertyBag,
                              sbILocalDatabaseResourcePropertyBag)

sbLocalDatabaseResourcePropertyBag::sbLocalDatabaseResourcePropertyBag(sbLocalDatabasePropertyCache* aCache,
                                                                       const nsAString &aGuid)
: mCache(aCache)
, mWritePending(PR_FALSE)
, mGuid(aGuid)
{
}

sbLocalDatabaseResourcePropertyBag::~sbLocalDatabaseResourcePropertyBag()
{
  if (mLock) {
    nsAutoLock::DestroyLock(mLock);
  }
}

nsresult
sbLocalDatabaseResourcePropertyBag::Init()
{
  PRBool success = mValueMap.Init(1000);
  NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);

  success = mDirty.Init();
  NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);

  mLock = nsAutoLock::NewLock("sbLocalDatabaseResourcePropertyBag::mLock");
  NS_ENSURE_TRUE(mLock, NS_ERROR_OUT_OF_MEMORY);

  return NS_OK;
}

PR_STATIC_CALLBACK(PLDHashOperator)
PropertyBagKeysToArray(const PRUint32& aPropertyID,
                       nsString* aValue,
                       void *aArg)
{
  nsTArray<PRUint32>* propertyIDs = (nsTArray<PRUint32>*) aArg;
  if (propertyIDs->AppendElement(aPropertyID)) {
    return PL_DHASH_NEXT;
  }
  else {
    return PL_DHASH_STOP;
  }
}

NS_IMETHODIMP
sbLocalDatabaseResourcePropertyBag::GetGuid(nsAString &aGuid)
{
  nsAutoLock lock(mLock);

  aGuid = mGuid;
  return NS_OK;
}

NS_IMETHODIMP 
sbLocalDatabaseResourcePropertyBag::GetWritePending(PRBool *aWritePending)
{
  NS_ENSURE_ARG_POINTER(aWritePending);

  nsAutoLock lock(mLock);

  *aWritePending = mWritePending;
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseResourcePropertyBag::GetNames(nsIStringEnumerator **aNames)
{
  NS_ENSURE_ARG_POINTER(aNames);

  nsAutoLock lock(mLock);

  nsTArray<PRUint32> propertyIDs;
  mValueMap.EnumerateRead(PropertyBagKeysToArray, &propertyIDs);

  PRUint32 len = mValueMap.Count();
  if (propertyIDs.Length() < len) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  nsTArray<nsString> propertyNames;
/*
  XXX: these commented out bits are waiting for NS_NewAdoptingStringEnumerator
       to be included in the glue
  nsStringArray *array = new nsStringArray(len);
  NS_ENSURE_TRUE(array, NS_ERROR_OUT_OF_MEMORY);
*/
  for (PRUint32 i = 0; i < len; i++) {
    nsAutoString propertyName;
    PRBool success = mCache->GetPropertyName(propertyIDs[i], propertyName);
    NS_ENSURE_TRUE(success, NS_ERROR_UNEXPECTED);
/*
    NS_ENSURE_TRUE(array->InsertStringAt(propertyName, i),
                   NS_ERROR_OUT_OF_MEMORY);
*/
    propertyNames.AppendElement(propertyName);
  }

/*
  NS_NewAdoptingStringEnumerator(aNames, array);
*/
  *aNames = new sbTArrayStringEnumerator(&propertyNames);
  NS_ENSURE_TRUE(*aNames, NS_ERROR_OUT_OF_MEMORY);
  NS_ADDREF(*aNames);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseResourcePropertyBag::GetProperty(const nsAString& aName,
                                                nsAString& _retval)
{
  nsAutoLock lock(mLock);

  PRUint32 propertyID = mCache->GetPropertyID(aName);

  return GetPropertyByID(propertyID, _retval);
}

NS_IMETHODIMP
sbLocalDatabaseResourcePropertyBag::GetPropertyByID(PRUint32 aPropertyID,
                                                    nsAString& _retval)
{
  if(aPropertyID > 0) {
    nsString* value;
    if (mValueMap.Get(aPropertyID, &value)) {
      _retval = *value;
      return NS_OK;
    }
  }

  return NS_ERROR_ILLEGAL_VALUE;
}

NS_IMETHODIMP 
sbLocalDatabaseResourcePropertyBag::SetProperty(const nsAString & aName, 
                                                const nsAString & aValue)
{
  PRUint32 propertyID = mCache->GetPropertyID(aName);
  return SetPropertyByID(propertyID, aValue);
}

NS_IMETHODIMP 
sbLocalDatabaseResourcePropertyBag::SetPropertyByID(PRUint32 aPropertyID, 
                                                    const nsAString & aValue)
{
  nsAutoLock lock(mLock);

  nsresult rv = NS_ERROR_INVALID_ARG;
  if(aPropertyID > 0) {
     rv = PutValue(aPropertyID, aValue);
     NS_ENSURE_SUCCESS(rv, rv);
     
     rv = mCache->AddDirtyGUID(mGuid);
     NS_ENSURE_SUCCESS(rv, rv);

     mWritePending = PR_TRUE;
     mDirty.PutEntry(aPropertyID);
  }

  return rv;
}

NS_IMETHODIMP sbLocalDatabaseResourcePropertyBag::Write()
{
  nsAutoLock lock(mLock);

  nsresult rv = NS_OK;

  if(mWritePending) {
    rv = mCache->Write();
    NS_ENSURE_SUCCESS(rv, rv);

    mWritePending = PR_FALSE;
  }

  return rv;
}

nsresult
sbLocalDatabaseResourcePropertyBag::PutValue(PRUint32 aPropertyID,
                                             const nsAString& aValue)
{
  nsAutoPtr<nsString> value(new nsString(aValue));
  PRBool success = mValueMap.Put(aPropertyID, value);
  NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);
  value.forget();

  return NS_OK;
}

nsresult
sbLocalDatabaseResourcePropertyBag::EnumerateDirty(nsTHashtable<nsUint32HashKey>::Enumerator aEnumFunc, 
                                                   void *aClosure, 
                                                   PRUint32 *aDirtyCount)
{
  NS_ENSURE_ARG_POINTER(aClosure);
  NS_ENSURE_ARG_POINTER(aDirtyCount);

  *aDirtyCount = mDirty.EnumerateEntries(aEnumFunc, aClosure);
  return NS_OK;
}

nsresult
sbLocalDatabaseResourcePropertyBag::SetDirty(PRBool aDirty)
{
  if(mWritePending && !aDirty) {
    mDirty.Clear();
  }

  mWritePending = aDirty;

  return NS_OK;
}

NS_IMPL_ISUPPORTS1(sbTArrayStringEnumerator,
                   nsIStringEnumerator)

sbTArrayStringEnumerator::sbTArrayStringEnumerator(nsTArray<nsString>* aStringArray) :
  mNextIndex(0)
{
  mStringArray.InsertElementsAt(0, *aStringArray);
}

NS_IMETHODIMP
sbTArrayStringEnumerator::HasMore(PRBool *_retval)
{
  *_retval = mNextIndex < mStringArray.Length();
  return NS_OK;
}

NS_IMETHODIMP
sbTArrayStringEnumerator::GetNext(nsAString& _retval)
{
  if (mNextIndex < mStringArray.Length()) {
    _retval = mStringArray[mNextIndex];
    mNextIndex++;
    return NS_OK;
  }
  else {
    return NS_ERROR_NOT_AVAILABLE;
  }
}

