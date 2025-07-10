import sensor, image, time, network, socket, struct, math

# WiFi配置
SSID = "ID"
PASSWORD = "PASSWORD"

# 服务器配置
SERVER_IP = "192.168.137.45"
SERVER_PORT = 8888

# 初始化摄像头
sensor.reset()
sensor.set_pixformat(sensor.GRAYSCALE)  # 灰度更快
sensor.set_framesize(sensor.QVGA)       # 320x240
sensor.skip_frames(time=500)            # 缩短等待时间

# 优化摄像头设置
sensor.set_auto_gain(False)
sensor.set_auto_whitebal(False)
sensor.set_auto_exposure(False, exposure_us=10000)
sensor.set_contrast(3)                  # 提高对比度有助于边缘检测

# 连接WiFi
wlan = network.WLAN(network.STA_IF)
wlan.active(True)
wlan.connect(SSID, PASSWORD)

while not wlan.isconnected():
    pass

print("Connected to WiFi. IP:", wlan.ifconfig()[0])

# 优化后的裂缝检测参数
MIN_CRACK_LENGTH = 50      # 最小裂缝长度(像素)
MIN_LINE_LENGTH = 20       # 线段最小长度
LINE_THRESHOLD = 1500      # 线段检测阈值(提高可减少噪声)
EDGE_THRESHOLD = (30, 70)  # 优化的Canny阈值

def detect_crack(img):
    # 1. 快速直方图均衡化
    img.histeq(adaptive=True)

    # 2. 直接进行线段检测(跳过单独的边缘检测)
    lines = img.find_lines(threshold=LINE_THRESHOLD,
                          theta_margin=25,
                          rho_margin=25,
                          segment_threshold=MIN_LINE_LENGTH)

    # 3. 计算特征
    total_length = sum(l.length() for l in lines)
    max_length = max((l.length() for l in lines), default=0)

    # 4. 判断条件
    return 0x01 if (total_length > MIN_CRACK_LENGTH and
                   max_length > MIN_LINE_LENGTH) else 0x02

def main():
    sock = socket.socket()
    try:
        sock.connect((SERVER_IP, SERVER_PORT))
        print("Connected to server")

        clock = time.clock()
        while True:
            clock.tick()

            # 捕获图像并直接处理(不复制)
            img = sensor.snapshot()
            status = detect_crack(img)
            sock.send(struct.pack('B', status))

            # 动态帧率控制
            fps = clock.fps()
            if fps > 5:  # 如果处理太快，可以添加小延迟
                time.sleep_ms(50)

            print("FPS:", fps)

    except Exception as e:
        print("Error:", e)
    finally:
        sock.close()
        print("Connection closed")

if __name__ == "__main__":
    main()
