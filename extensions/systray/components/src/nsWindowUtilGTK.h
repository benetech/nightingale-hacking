/* vim: fileencoding=utf-8 shiftwidth=2 */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is toolkit/components/systray
 *
 * The Initial Developer of the Original Code is
 * Mook <mook.moz+cvs.mozilla.org@gmail.com>.
 * Portions created by the Initial Developer are Copyright (C) 2007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Brad Peterson <b_peterson@yahoo.com>
 *   Daniel Glazman <daniel.glazman@disruptive-innovations.com>
 *   Matthew Gertner <matthew@allpeers.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#ifndef nsWindowUtilGTK_h__
#define nsWindowUtilGTK_h__

#include "nsIWindowUtil.h"
#include "nsIDOMWindow.h"
#include "nsCOMPtr.h"
#include <gdk/gdk.h>

class nsWindowUtil : public nsIWindowUtil
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIWINDOWUTIL

  nsWindowUtil();

private:
  ~nsWindowUtil();

protected:
  nsCOMPtr<nsIDOMWindow> mDOMWindow;
  GdkWindow *mGDKWindow;
};

#define NS_WINDOWUTIL_CONTRACTID  "@mozilla.org/window-util;1"
#define NS_WINDOWUTIL_CLASSNAME   "Window Util"

// 11eae780-257b-4480-8b89-b6b91d166438
#define NS_WINDOWUTIL_CID \
  { 0x11eae780, 0x257b, 0x4480, \
  { 0x8b, 0x89, 0xb6, 0xb9, 0x1d, 0x16, 0x64, 0x38 } }

#endif /* nsWindowUtilGTK_h__ */