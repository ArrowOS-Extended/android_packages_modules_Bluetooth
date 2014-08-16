/******************************************************************************
 *
 *  Copyright (C) 2014 Google, Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

#define LOG_TAG "bt_vendor"

#include <assert.h>
#include <dlfcn.h>
#include <utils/Log.h>

#include "bt_vendor_lib.h"
#include "osi.h"
#include "vendor.h"

#define LAST_VENDOR_OPCODE_VALUE VENDOR_DO_EPILOG

static const char *VENDOR_LIBRARY_NAME = "libbt-vendor.so";
static const char *VENDOR_LIBRARY_SYMBOL_NAME = "BLUETOOTH_VENDOR_LIB_INTERFACE";

static const vendor_interface_t interface;
static const allocator_t *allocator;
static vendor_cb callbacks[LAST_VENDOR_OPCODE_VALUE + 1];
static send_internal_command_cb send_internal_command_callback;

static void *lib_handle;
static bt_vendor_interface_t *lib_interface;
static const bt_vendor_callbacks_t lib_callbacks;

// Interface functions

static bool vendor_open(const uint8_t *local_bdaddr, const allocator_t *buffer_allocator) {
  assert(lib_handle == NULL);
  allocator = buffer_allocator;

  lib_handle = dlopen(VENDOR_LIBRARY_NAME, RTLD_NOW);
  if (!lib_handle) {
    ALOGE("%s unable to open %s: %s", __func__, VENDOR_LIBRARY_NAME, dlerror());
    goto error;
  }

  lib_interface = (bt_vendor_interface_t *)dlsym(lib_handle, VENDOR_LIBRARY_SYMBOL_NAME);
  if (!lib_interface) {
    ALOGE("%s unable to find symbol %s in %s: %s", __func__, VENDOR_LIBRARY_SYMBOL_NAME, VENDOR_LIBRARY_NAME, dlerror());
    goto error;
  }

  ALOGI("alloc value %p", lib_callbacks.alloc);

  int status = lib_interface->init(&lib_callbacks, (unsigned char *)local_bdaddr);
  if (status) {
    ALOGE("%s unable to initialize vendor library: %d", __func__, status);
    goto error;
  }

  return true;

error:;
  lib_interface = NULL;
  if (lib_handle)
    dlclose(lib_handle);
  lib_handle = NULL;
  return false;
}

static void vendor_close(void) {
  if (lib_interface)
    lib_interface->cleanup();

  if (lib_handle)
    dlclose(lib_handle);

  lib_interface = NULL;
  lib_handle = NULL;
}

static int send_command(vendor_opcode_t opcode, void *param) {
  assert(lib_interface != NULL);
  return lib_interface->op(opcode, param);
}

static int send_async_command(vendor_async_opcode_t opcode, void *param) {
  assert(lib_interface != NULL);
  return lib_interface->op(opcode, param);
}

static void set_callback(vendor_async_opcode_t opcode, vendor_cb callback) {
  callbacks[opcode] = callback;
}

static void set_send_internal_command_callback(send_internal_command_cb callback) {
  send_internal_command_callback = callback;
}

// Internal functions

// Called back from vendor library when the firmware configuration
// completes.
static void firmware_config_cb(bt_vendor_op_result_t result) {
  ALOGI("firmware callback");
  vendor_cb callback = callbacks[VENDOR_CONFIGURE_FIRMWARE];
  assert(callback != NULL);
  callback(result == BT_VND_OP_RESULT_SUCCESS);
}

// Called back from vendor library to indicate status of previous
// SCO configuration request. This should only happen during the
// postload process.
static void sco_config_cb(bt_vendor_op_result_t result) {
  ALOGI("%s", __func__);
  vendor_cb callback = callbacks[VENDOR_CONFIGURE_SCO];
  assert(callback != NULL);
  callback(result == BT_VND_OP_RESULT_SUCCESS);
}

// Called back from vendor library to indicate status of previous
// LPM enable/disable request.
static void low_power_mode_cb(bt_vendor_op_result_t result) {
  ALOGI("%s", __func__);
  vendor_cb callback = callbacks[VENDOR_SET_LPM_MODE];
  assert(callback != NULL);
  callback(result == BT_VND_OP_RESULT_SUCCESS);
}

/******************************************************************************
**
** Function         sco_audiostate_cb
**
** Description      HOST/CONTROLLER VENDOR LIB CALLBACK API - This function is
**                  called when the libbt-vendor completed vendor specific codec
**                  setup request
**
** Returns          None
**
******************************************************************************/
static void sco_audiostate_cb(bt_vendor_op_result_t result)
{
    uint8_t status = (result == BT_VND_OP_RESULT_SUCCESS) ? 0 : 1;

    ALOGI("sco_audiostate_cb(status: %d)",status);
}

// Called by vendor library when it needs an HCI buffer.
static void *buffer_alloc_cb(int size) {
  return allocator->alloc(size);
}

// Called by vendor library when it needs to free a buffer allocated with
// |buffer_alloc_cb|.
static void buffer_free_cb(void *buffer) {
  allocator->free(buffer);
}

// Called back from vendor library when it wants to send an HCI command.
static uint8_t transmit_cb(uint16_t opcode, void *buffer, tINT_CMD_CBACK callback) {
  assert(send_internal_command_callback != NULL);
  return send_internal_command_callback(opcode, (BT_HDR *)buffer, callback);
}

// Called back from vendor library when the epilog procedure has
// completed. It is safe to call vendor_interface->cleanup() after
// this callback has been received.
static void epilog_cb(bt_vendor_op_result_t result) {
  ALOGI("%s", __func__);
  vendor_cb callback = callbacks[VENDOR_DO_EPILOG];
  assert(callback != NULL);
  callback(result == BT_VND_OP_RESULT_SUCCESS);
}

static const bt_vendor_callbacks_t lib_callbacks = {
  sizeof(lib_callbacks),
  firmware_config_cb,
  sco_config_cb,
  low_power_mode_cb,
  sco_audiostate_cb,
  buffer_alloc_cb,
  buffer_free_cb,
  transmit_cb,
  epilog_cb
};

static const vendor_interface_t interface = {
  vendor_open,
  vendor_close,
  send_command,
  send_async_command,
  set_callback,
  set_send_internal_command_callback
};

const vendor_interface_t *vendor_get_interface() {
  return &interface;
}
