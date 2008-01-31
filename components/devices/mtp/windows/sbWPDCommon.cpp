/*
 //
 // BEGIN SONGBIRD GPL
 //
 // This file is part of the Songbird web player.
 //
 // Copyright(c) 2005-2008 POTI, Inc.
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

#include "sbWPDCommon.h"
#include <nsServiceManagerUtils.h>
#include <nsComponentManagerUtils.h>
#include <sbIDeviceMarshall.h>
#include <sbIDeviceEventTarget.h>
#include <sbIDeviceManager.h>
#include <sbIDevice.h>
#include <nsIVariant.h>
#include <sbIDeviceEvent.h>

/**
 * Create the Songbird Device manager and return it
 */
nsresult sbWPDCreateDeviceManager(sbIDeviceManager2 ** deviceManager)
{
  NS_ENSURE_ARG(deviceManager);
  nsresult rv;
  nsCOMPtr<sbIDeviceManager2> dm(do_GetService("@songbirdnest.com/Songbird/DeviceManager;2", &rv));
  dm.forget(deviceManager);
  return rv;
}

/**
 * This creates an event for the device and dispatches it to the manager
 */
nsresult sbWPDCreateAndDispatchEvent(sbIDeviceMarshall * marshall,
                                         sbIDevice * device,
                                         PRUint32 eventID,
                                         PRBool async)
{  
  // Create the device manager
  nsCOMPtr<sbIDeviceManager2> manager;
  nsresult rv = sbWPDCreateDeviceManager(getter_AddRefs(manager));
  NS_ENSURE_SUCCESS(rv, rv);
  
  // QI it to the event target
  nsCOMPtr<sbIDeviceEventTarget> target = do_QueryInterface(manager);
  if (!manager || !target)
    return NS_ERROR_FAILURE;

  // Create a variant to hold the device
  nsCOMPtr<nsIWritableVariant> var = do_CreateInstance(NS_VARIANT_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  var->SetAsISupports(device);

  // Create the event
  nsCOMPtr<sbIDeviceEvent> event;
  rv = manager->CreateEvent(eventID, var, marshall, getter_AddRefs(event));
  NS_ENSURE_SUCCESS(rv, rv);
  
  PRBool dispatched;
  // Dispatch the event
  return target->DispatchEvent(event, async, &dispatched);
}
