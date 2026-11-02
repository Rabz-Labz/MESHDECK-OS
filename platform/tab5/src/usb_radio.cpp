// usb_radio.cpp - see usb_radio.h.
#include "usb_radio.h"
#include <M5Unified.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

namespace {

UsbRadio* instance = nullptr;
portMUX_TYPE ringLock = portMUX_INITIALIZER_UNLOCKED;

void hostTask(void*) {
    for (;;) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    }
}

void clientTask(void* arg) { static_cast<UsbRadio*>(arg)->clientLoop(); }

void clientEvent(const usb_host_client_event_msg_t* msg, void* arg) {
    auto* self = static_cast<UsbRadio*>(arg);
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) self->onClientEvent(0, msg->new_dev.address);
    else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) self->onClientEvent(1, 0);
}

void inCallback(usb_transfer_t* t) { static_cast<UsbRadio*>(t->context)->onInDone(t); }
void outCallback(usb_transfer_t* t) { static_cast<UsbRadio*>(t->context)->onOutDone(t); }

} // namespace

bool UsbRadio::begin() {
    instance = this;
    M5.Power.setExtOutput(true, m5::ext_port_mask_t::ext_USB);   // USB-A 5 V (IO expander USB5V_EN)
    usb_host_config_t cfg = {};
    cfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
    if (usb_host_install(&cfg) != ESP_OK) return false;
    outDone_ = xSemaphoreCreateBinary();
    usb_host_client_config_t client = {};
    client.is_synchronous = false;
    client.max_num_event_msg = 5;
    client.async.client_event_callback = clientEvent;
    client.async.callback_arg = this;
    usb_host_client_handle_t handle;
    if (usb_host_client_register(&client, &handle) != ESP_OK) return false;
    client_ = handle;
    xTaskCreatePinnedToCore(hostTask, "usb_host", 4096, nullptr, 5, nullptr, 0);
    xTaskCreatePinnedToCore(clientTask, "usb_radio", 4096, this, 5, nullptr, 0);
    return true;
}

void UsbRadio::onClientEvent(int event, uint8_t address) {
    if (event == 0) pendingAddress_ = address;
    else { gone_ = true; ready_ = false; }
}

void UsbRadio::clientLoop() {
    for (;;) {
        usb_host_client_handle_events((usb_host_client_handle_t)client_, pdMS_TO_TICKS(50));
        if (gone_) { gone_ = false; closeDevice(); }
        if (pendingAddress_ && !device_) { uint8_t a = pendingAddress_; pendingAddress_ = 0; openDevice(a); }
    }
}

void UsbRadio::openDevice(uint8_t address) {
    usb_device_handle_t dev;
    if (usb_host_device_open((usb_host_client_handle_t)client_, address, &dev) != ESP_OK) return;
    const usb_device_desc_t* dd;
    const usb_config_desc_t* cd;
    if (usb_host_get_device_descriptor(dev, &dd) != ESP_OK || usb_host_get_active_config_descriptor(dev, &cd) != ESP_OK) {
        usb_host_device_close((usb_host_client_handle_t)client_, dev);
        return;
    }
    vid_ = dd->idVendor;
    pid_ = dd->idProduct;

    // Walk the configuration: remember the CDC communication interface (class 0x02) and the
    // CDC data interface (class 0x0A) with its bulk IN and OUT endpoints.
    int offset = 0;
    const usb_standard_desc_t* d = (const usb_standard_desc_t*)cd;
    int currentClass = -1, currentIntf = -1;
    bool found = false;
    epIn_ = epOut_ = 0;
    while ((d = usb_parse_next_descriptor(d, cd->wTotalLength, &offset)) != nullptr) {
        if (d->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            const usb_intf_desc_t* in = (const usb_intf_desc_t*)d;
            currentClass = in->bInterfaceClass;
            currentIntf = in->bInterfaceNumber;
            if (currentClass == 0x02) commIntf_ = in->bInterfaceNumber;
        } else if (d->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT && currentClass == 0x0A) {
            const usb_ep_desc_t* ep = (const usb_ep_desc_t*)d;
            if ((ep->bmAttributes & 0x03) != 0x02) continue;   // bulk only
            if (ep->bEndpointAddress & 0x80) { epIn_ = ep->bEndpointAddress; mpsIn_ = ep->wMaxPacketSize; }
            else epOut_ = ep->bEndpointAddress;
            dataIntf_ = (uint8_t)currentIntf;
            found = epIn_ && epOut_;
        }
        if (found) break;
    }
    if (!found || usb_host_interface_claim((usb_host_client_handle_t)client_, dev, dataIntf_, 0) != ESP_OK) {
        usb_host_device_close((usb_host_client_handle_t)client_, dev);
        return;
    }
    device_ = dev;

    // SET_CONTROL_LINE_STATE: DTR on, RTS off.
    usb_transfer_t* ctrl;
    if (usb_host_transfer_alloc(sizeof(usb_setup_packet_t), 0, &ctrl) == ESP_OK) {
        usb_setup_packet_t* setup = (usb_setup_packet_t*)ctrl->data_buffer;
        setup->bmRequestType = 0x21;
        setup->bRequest = 0x22;
        setup->wValue = 0x0001;
        setup->wIndex = commIntf_;
        setup->wLength = 0;
        ctrl->num_bytes = sizeof(usb_setup_packet_t);
        ctrl->device_handle = dev;
        ctrl->bEndpointAddress = 0;
        ctrl->callback = outCallback;
        ctrl->context = this;
        ctrlResult_ = usb_host_transfer_submit_control((usb_host_client_handle_t)client_, ctrl);
        if (ctrlResult_ == ESP_OK) {
            for (int i = 0; i < 10 && xSemaphoreTake((SemaphoreHandle_t)outDone_, 0) != pdTRUE; i++)
                usb_host_client_handle_events((usb_host_client_handle_t)client_, pdMS_TO_TICKS(20));
            ctrlResult_ = 1000 + ctrl->status;   // 1000 = completed
        }
        usb_host_transfer_free(ctrl);
    }

    usb_transfer_t* in;
    usb_transfer_t* out;
    usb_host_transfer_alloc(512, 0, &in);
    usb_host_transfer_alloc(1024, 0, &out);
    in->device_handle = out->device_handle = dev;
    in->bEndpointAddress = epIn_;
    out->bEndpointAddress = epOut_;
    in->callback = inCallback;
    out->callback = outCallback;
    in->context = out->context = this;
    in->num_bytes = (512 / mpsIn_) * mpsIn_;
    inXfer_ = in;
    outXfer_ = out;
    // Start with an empty ring. Each volatile member is set with a plain assignment: C++20 deprecates
    // ++ and chained assignment on volatile, and the callbacks and the main loop share these values.
    head_ = 0;
    tail_ = 0;
    ready_ = true;
    inSubmit_ = usb_host_transfer_submit(in);
}

void UsbRadio::closeDevice() {
    ready_ = false;
    if (!device_) return;
    usb_device_handle_t dev = (usb_device_handle_t)device_;
    // Let outstanding transfers complete (with NO_DEVICE) before freeing them.
    for (int i = 0; i < 5; i++) usb_host_client_handle_events((usb_host_client_handle_t)client_, pdMS_TO_TICKS(20));
    usb_host_interface_release((usb_host_client_handle_t)client_, dev, dataIntf_);
    if (inXfer_) usb_host_transfer_free((usb_transfer_t*)inXfer_);
    if (outXfer_) usb_host_transfer_free((usb_transfer_t*)outXfer_);
    inXfer_ = outXfer_ = nullptr;
    usb_host_device_close((usb_host_client_handle_t)client_, dev);
    device_ = nullptr;
    vid_ = pid_ = 0;
}

void UsbRadio::onInDone(void* transfer) {
    usb_transfer_t* t = (usb_transfer_t*)transfer;
    lastInStatus_ = t->status;
    if (t->status != USB_TRANSFER_STATUS_COMPLETED) inErrors_ = inErrors_ + 1;   // count the failed transfer
    if (t->status == USB_TRANSFER_STATUS_COMPLETED) {
        rxBytes_ += t->actual_num_bytes;
        // Keep the first bytes received, for the diagnostics line (up to the size of firstBytes_).
        for (int i = 0; i < t->actual_num_bytes && firstCount_ < (int)sizeof firstBytes_; i++) {
            firstBytes_[firstCount_] = t->data_buffer[i];
            firstCount_ = firstCount_ + 1;
        }
        portENTER_CRITICAL(&ringLock);
        for (int i = 0; i < t->actual_num_bytes; i++) {
            int next = (head_ + 1) % kRing;
            if (next == tail_) break;   // full: drop (the mesh parser resynchronises on frame headers)
            ring_[head_] = t->data_buffer[i];
            head_ = next;
        }
        portEXIT_CRITICAL(&ringLock);
    }
    if (ready_ && t->status != USB_TRANSFER_STATUS_NO_DEVICE && t->status != USB_TRANSFER_STATUS_CANCELED)
        usb_host_transfer_submit(t);
}

void UsbRadio::onOutDone(void*) { xSemaphoreGive((SemaphoreHandle_t)outDone_); }

int UsbRadio::read(uint8_t* buf, int max) {
    int n = 0;
    portENTER_CRITICAL(&ringLock);
    while (n < max && tail_ != head_) { buf[n++] = ring_[tail_]; tail_ = (tail_ + 1) % kRing; }
    portEXIT_CRITICAL(&ringLock);
    return n;
}

bool UsbRadio::write(const uint8_t* data, int len) {
    if (!ready_ || !outXfer_ || len > 1024) return false;
    usb_transfer_t* t = (usb_transfer_t*)outXfer_;
    xSemaphoreTake((SemaphoreHandle_t)outDone_, 0);   // clear any stale completion
    memcpy(t->data_buffer, data, len);
    t->num_bytes = len;
    if (usb_host_transfer_submit(t) != ESP_OK) { txFail_ = txFail_ + 1; return false; }   // could not start: count it
    bool ok = xSemaphoreTake((SemaphoreHandle_t)outDone_, pdMS_TO_TICKS(500)) == pdTRUE &&
              t->status == USB_TRANSFER_STATUS_COMPLETED;
    // Count the outcome: a sent frame, or one that failed or timed out.
    if (ok) txOk_ = txOk_ + 1;
    else txFail_ = txFail_ + 1;
    return ok;
}

std::string UsbRadio::diagnostics() const {
    char buf[300];
    int n = snprintf(buf, sizeof buf,
                     "data_if=%u comm_if=%u ep_in=%02X ep_out=%02X mps=%u ctrl=%d in_submit=%d rx=%u tx_ok=%u tx_fail=%u "
                     "in_err=%u in_status=%d first=",
                     dataIntf_, commIntf_, epIn_, epOut_, mpsIn_, ctrlResult_, inSubmit_, (unsigned)rxBytes_,
                     (unsigned)txOk_, (unsigned)txFail_, (unsigned)inErrors_, lastInStatus_);
    for (int i = 0; i < firstCount_ && n < (int)sizeof buf - 3; i++) n += snprintf(buf + n, sizeof buf - n, "%02X", firstBytes_[i]);
    return buf;
}

std::string UsbRadio::describe() const {
    if (!ready_) return "NONE";
    char buf[32];
    snprintf(buf, sizeof buf, "%04X:%04X CDC", vid_, pid_);
    return buf;
}
