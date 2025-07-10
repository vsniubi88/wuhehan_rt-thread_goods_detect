/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-07-09     86188       the first version
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdev.h>
#include <wlan_mgnt.h>

#define SERVER_PORT 8888
#define BUF_SIZE 1024

// WiFi配置
#define WIFI_SSID     "ID"
#define WIFI_PASSWORD "PASSWORD"

// 硬件配置（根据实际引脚修改）
#define LED_PIN       GET_PIN(F, 11)   // 假设LED接在PF11

// 全局变量声明
static int sock = -1;
static int conn = -1;
static volatile int crack_detected = 0; // 裂缝状态标志

/* 函数声明 */
static void wifi_event_handler(int event, struct rt_wlan_buff *buff, void *parameter);
static int wifi_init(void);
static void led_thread_entry(void *parameter);
static void lcd_thread_entry(void *parameter);
static void tcp_server_thread_entry(void *parameter);

/* WiFi连接状态回调 */
static void wifi_event_handler(int event, struct rt_wlan_buff *buff, void *parameter) {
    switch (event) {
        case RT_WLAN_EVT_READY:
            rt_kprintf("[WiFi] PHY layer ready\n");
            break;
        case RT_WLAN_EVT_SCAN_DONE:
            rt_kprintf("[WiFi] Scan completed\n");
            break;
        case RT_WLAN_EVT_STA_CONNECTED:
            rt_kprintf("[WiFi] Connected to AP\n");
            break;
        case RT_WLAN_EVT_STA_DISCONNECTED:
            rt_kprintf("[WiFi] Disconnected from AP\n");
            break;
        case RT_WLAN_EVT_STA_CONNECTED_FAIL:
            rt_kprintf("[WiFi] Connection failed\n");
            break;
    }
}

/* 初始化WiFi连接 */
static int wifi_init(void) {
    rt_wlan_register_event_handler(RT_WLAN_EVT_READY, wifi_event_handler, RT_NULL);
    rt_wlan_register_event_handler(RT_WLAN_EVT_STA_CONNECTED, wifi_event_handler, RT_NULL);
    rt_wlan_register_event_handler(RT_WLAN_EVT_STA_DISCONNECTED, wifi_event_handler, RT_NULL);
    rt_wlan_register_event_handler(RT_WLAN_EVT_STA_CONNECTED_FAIL, wifi_event_handler, RT_NULL);

    if (rt_wlan_set_mode(RT_WLAN_DEVICE_STA_NAME, RT_WLAN_STATION) != RT_EOK) {
        rt_kprintf("[WiFi] Set STA mode failed\n");
        return -1;
    }

    rt_kprintf("[WiFi] Connecting to %s...\n", WIFI_SSID);
    if (rt_wlan_connect(WIFI_SSID, WIFI_PASSWORD) != RT_EOK) {
        rt_kprintf("[WiFi] Connect command failed\n");
        return -1;
    }

    for (int i = 0; i < 20; i++) {
        if (rt_wlan_is_connected()) {
            rt_kprintf("[WiFi] Connection established\n");
            return 0;
        }
        rt_thread_mdelay(500);
    }

    rt_kprintf("[WiFi] Connection timeout\n");
    return -1;
}

/* LED闪烁线程 */
static void led_thread_entry(void *parameter) {
    rt_pin_mode(LED_PIN, PIN_MODE_OUTPUT);
    while (1) {
        if (crack_detected) {
            rt_pin_write(LED_PIN, PIN_HIGH);  // LED亮
            rt_thread_mdelay(200);
            rt_pin_write(LED_PIN, PIN_LOW);   // LED灭
            rt_thread_mdelay(200);
        } else {
            rt_pin_write(LED_PIN, PIN_LOW);  // LED保持熄灭
            rt_thread_mdelay(1000);
        }
    }
}

/* LCD显示线程 */
static void lcd_thread_entry(void *parameter) {
    while (1) {
        if (crack_detected) {
            rt_kprintf("[LCD] ALERT: CRACK DETECTED!\n");
        } else {
            rt_kprintf("[LCD] Status: Normal\n");
        }
        rt_thread_mdelay(500);
    }
}

/* TCP服务器线程 */
static void tcp_server_thread_entry(void *parameter) {
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        rt_kprintf("[TCP] Socket create failed\n");
        return;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    rt_memset(&(server_addr.sin_zero), 0, sizeof(server_addr.sin_zero));

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        rt_kprintf("[TCP] Bind failed\n");
        closesocket(sock);
        return;
    }

    if (listen(sock, 5) < 0) {
        rt_kprintf("[TCP] Listen failed\n");
        closesocket(sock);
        return;
    }

    rt_kprintf("[TCP] Server started on port %d\n", SERVER_PORT);

    while (1) {
        if ((conn = accept(sock, (struct sockaddr *)&client_addr, &client_len)) < 0) {
            rt_kprintf("[TCP] Accept failed\n");
            continue;
        }

        char ip_str[16];
        rt_snprintf(ip_str, sizeof(ip_str), "%s", inet_ntoa(client_addr.sin_addr));
        rt_kprintf("[TCP] Client connected: %s\n", ip_str);

        while (1) {
            int status = 0;
            if (recv(conn, &status, 1, 0) <= 0) {
                rt_kprintf("[TCP] Connection closed\n");
                break;
            }

            // 更新裂缝状态
            crack_detected = (status == 0x01);
            rt_kprintf("[STATUS] %s\n", crack_detected ? "CRACK!" : "Normal");
        }

        closesocket(conn);
        conn = -1;
    }

    closesocket(sock);
    sock = -1;
}

int main(void) {
    /* 初始化WiFi */
    rt_kprintf("\n===== System Boot =====\n");
    if (wifi_init() != 0) {
        rt_kprintf("[ERR] WiFi init failed!\n");
        return -1;
    }

    /* 打印网络信息 */
    struct netdev *netdev = netdev_get_first_by_flags(NETDEV_FLAG_LINK_UP | NETDEV_FLAG_INTERNET_UP);
    if (netdev) {
        rt_kprintf("[NET] IP: %s\n", inet_ntoa(netdev->ip_addr));
    }

    /* 创建LED线程 */
    rt_thread_t led_tid = rt_thread_create("led", led_thread_entry, RT_NULL, 512, 20, 10);
    if (led_tid) rt_thread_startup(led_tid);

    /* 创建LCD线程 */
    rt_thread_t lcd_tid = rt_thread_create("lcd", lcd_thread_entry, RT_NULL, 1024, 15, 10);
    if (lcd_tid) rt_thread_startup(lcd_tid);

    /* 创建TCP服务器线程 */
    rt_thread_t tcp_tid = rt_thread_create("tcp_server", tcp_server_thread_entry, RT_NULL, 4096, 25, 10);
    if (tcp_tid) rt_thread_startup(tcp_tid);

    return 0;
}
