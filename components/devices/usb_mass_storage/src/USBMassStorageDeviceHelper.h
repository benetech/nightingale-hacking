/*
//
// BEGIN SONGBIRD GPL
// 
// This file is part of the Songbird web player.
//
// Copyright 2006 POTI, Inc.
// http://songbirdnest.com
// 
// This file may be licensed under the terms of of the
// GNU General Public License Version 2 (the GPL).
// 
// Software distributed under the License is distributed 
// on an AS IS basis, WITHOUT WARRANTY OF ANY KIND, either 
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

#ifndef __USB_MASS_STORAGE_DEVICE_HELPER_H__
#define __USB_MASS_STORAGE_DEVICE_HELPER_H__

/** 
* \file  USBMassStorageDeviceHelper.h
* \brief Songbird USBMassStorageDeviceHelper Definition.
*/

class IUSBMassStorageDeviceHelper
{
public:
  IUSBMassStorageDeviceHelper() {};
  ~IUSBMassStorageDeviceHelper() {};

  virtual PRBool Initialize(const nsAString &deviceName, const nsAString &deviceIdentifier) = 0;

  virtual const nsAString & GetDeviceVendor() = 0;
  virtual const nsAString & GetDeviceModel() = 0;
  virtual const nsAString & GetDeviceSerialNumber() = 0;

  virtual PRInt64 GetDeviceCapacity() = 0;
};

class CUSBMassStorageDeviceHelperStub : public IUSBMassStorageDeviceHelper
{
public:
  CUSBMassStorageDeviceHelperStub() {};
  ~CUSBMassStorageDeviceHelperStub() {};

  virtual PRBool Initialize(const nsAString &deviceName, const nsAString &deviceIdentifier){ return PR_FALSE; };

  virtual const nsAString & GetDeviceVendor() { return NS_LITERAL_STRING("__STUB__"); };
  virtual const nsAString & GetDeviceModel() { return NS_LITERAL_STRING("__STUB__"); };
  virtual const nsAString & GetDeviceSerialNumber() { return NS_LITERAL_STRING("__STUB__"); };

  virtual PRInt64 GetDeviceCapacity() { return -1; };
};

#endif // __USB_MASS_STORAGE_DEVICE_HELPER_H__

