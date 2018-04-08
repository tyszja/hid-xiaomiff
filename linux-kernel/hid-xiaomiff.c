/*
 * Force feedback support for Xiaomi game controllers like:
 * 2717:3144 Xiaomi bluetooth game controller
 *
 * Copyright (c) 2018 tyszja <tyszja@gmail.com>
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/hid.h>
#include <linux/module.h>

#define USB_VENDOR_ID_XIAOMI        0x2717
#define USB_VENDOR_ID_XIAOMI_MIPAD  0x3144

#define XIOAMIFF_MSG_LENGTH     6

struct xiaomiff_device {
    struct hid_field *field;
    struct hid_device *hdev;

    struct work_struct work;	
};

static int xiaomiff_play(struct input_dev *dev, void *data,
                                                    struct ff_effect *effect)
{
    struct xiaomiff_device *xiaomiff = data;
    int weak, strong;	

    strong = effect->u.rumble.strong_magnitude;
    strong = strong * 0xff / 0xffff;

    weak = effect->u.rumble.weak_magnitude;
    weak = weak * 0xff / 0xffff;

    dbg_hid("ff running with 0x%02x 0x%02x", strong, weak);

    xiaomiff->field->value[0] = weak;
    xiaomiff->field->value[1] = strong;

    schedule_work(&xiaomiff->work);

    return 0;
}

static void xiaomiff_worker(struct work_struct *work)
{
    struct xiaomiff_device *xiaomiff = container_of(work,
                                                struct xiaomiff_device, work);

    hid_hw_request(xiaomiff->hdev, xiaomiff->field->report, HID_REQ_SET_REPORT);
}

static int xiaomiff_init(struct hid_device *hdev)
{
    struct xiaomiff_device *xiaomiff;
    struct hid_report *report;
    struct hid_input *hidinput = list_first_entry(&hdev->inputs,
                                                    struct hid_input, list);
    struct list_head *report_list =
                            &hdev->report_enum[HID_FEATURE_REPORT].report_list;
    struct input_dev *dev = hidinput->input;
    int error;
    int i;

    if (list_empty(report_list)) {
        hid_err(hdev, "no feature reports found\n");
        return -ENODEV;
    }

    report = list_first_entry(report_list, struct hid_report, list);

    if (report->maxfield < 1 ||
                    report->field[0]->report_count != XIOAMIFF_MSG_LENGTH) {
        hid_err(hdev, "unexpected output report layout\n");
        return -ENODEV;
    }

    xiaomiff = kzalloc(sizeof(struct xiaomiff_device), GFP_KERNEL);
    if (!xiaomiff)
        return -ENOMEM;

    hid_set_drvdata(hdev, xiaomiff);
    xiaomiff->field = report->field[0];
    xiaomiff->hdev = hdev;

    set_bit(FF_RUMBLE, dev->ffbit);

    error = input_ff_create_memless(dev, xiaomiff, xiaomiff_play);
    if (error) {
        kfree(xiaomiff);
        return error;
    }

    /* Reset rumble on init. */
    for (i = 0; i < XIOAMIFF_MSG_LENGTH; i++) {
        report->field[0]->value[i] = 0;
    }

    hid_hw_request(hdev, report->field[0]->report, HID_REQ_SET_REPORT);
    hid_hw_wait(hdev);

    INIT_WORK(&xiaomiff->work, xiaomiff_worker);

    return 0;
}

static int xiaomi_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
    int ret;

    dbg_hid("Xiaomi Probe\r\n");

    ret = hid_parse(hdev);
    if (ret) {
        hid_err(hdev, "parse failed\n");
        goto err;
    }

    ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT & ~HID_CONNECT_FF);
    if (ret) {
        hid_err(hdev, "hw start failed\n");
        goto err;
    }
    
    xiaomiff_init(hdev);

    return 0;
err:
    return ret;
}

static void xiaomi_remove(struct hid_device *hdev)
{
    struct xiaomiff_device *xiaomiff = hid_get_drvdata(hdev);

    cancel_work_sync(&xiaomiff->work);

    hid_set_drvdata(hdev, NULL);

    hid_hw_close(hdev);
    hid_hw_stop(hdev);
}

static const struct hid_device_id xiaomi_devices[] = {
    { HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_XIAOMI, USB_VENDOR_ID_XIAOMI_MIPAD), },
    { }
};
MODULE_DEVICE_TABLE(hid, xiaomi_devices);

static struct hid_driver xiaomi_driver = {
    .name		= "hid-xiaomiff",
    .id_table	= xiaomi_devices,
    .probe		= xiaomi_probe,
    .remove     = xiaomi_remove,
#ifdef CONFIG_PM
    // TODO: Verify if we should stop rumble on suspend.
    // .suspend          = xiaomi_suspend,
    // .resume	         = xiaomi_resume,
    // .reset_resume     = xiaomi_resume,
#endif
};
module_hid_driver(xiaomi_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("tyszja <tyszja@gmail.com>");
MODULE_DESCRIPTION("Force feedback support for XIAOMI game controllers");
