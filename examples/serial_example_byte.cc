#include <iostream>
#include <iomanip>
#include <vector>
#include <cstring>
#include <thread>
#include <signal.h>
#include <serial/serial.h>

using namespace std;

serial::Serial* my_serial = nullptr;  // 定义全局串口对象

// 数据解析函数
int parseData(const vector<uint8_t> &data) {
    if (data.size() < 28) {
        cerr << "Invalid data length: " << data.size() << endl;
        return -1;
    }

    // 打印开头的数据
    std::cout << std::hex << (int)data[0] << " " << std::endl;

    // Check frame start, identifier, and end bytes
    if (data[0] != 0x48 && data[0] != 0x49) {
        cerr << "Invalid start byte" << endl;
        return -1;
    }

    if (data[1] != 0xAA || data[26] != 0x0D || data[27] != 0x0A) {
        cerr << "Invalid frame structure" << endl;
        return -1;
    }

    // Function to convert bytes to a float (assuming little-endian)
    auto bytesToFloat = [](uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
        uint32_t intVal = (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
        float value;
        memcpy(&value, &intVal, sizeof(float));
        return value;
    };

    // Parse force and moment values
    float Fx = bytesToFloat(data[2], data[3], data[4], data[5]);
    float Fy = bytesToFloat(data[6], data[7], data[8], data[9]);
    float Fz = bytesToFloat(data[10], data[11], data[12], data[13]);
    float Mx = bytesToFloat(data[14], data[15], data[16], data[17]);
    float My = bytesToFloat(data[18], data[19], data[20], data[21]);
    float Mz = bytesToFloat(data[22], data[23], data[24], data[25]);

    // Output the parsed values
    cout << fixed << setprecision(5);
    cout << "Fx: " << Fx << " Kg" << endl;
    cout << "Fy: " << Fy << " Kg" << endl;
    cout << "Fz: " << Fz << " Kg" << endl;
    cout << "Mx: " << Mx << " Kg•m" << endl;
    cout << "My: " << My << " Kg•m" << endl;
    cout << "Mz: " << Mz << " Kg•m" << endl;
    return 0;
}
void receiveDataBybyte() {
    std::vector<uint8_t> buffer;

    while (true) {
        uint8_t byte;
        my_serial->read(&byte, 1);  // 每次读取一个字节
        buffer.push_back(byte);

        // 检查是否有完整的帧（至少28字节）
        while (buffer.size() >= 28) {
            // 找到帧的起始字节和标识符
            if ((buffer[0] == 0x48 || buffer[0] == 0x49) && buffer[1] == 0xAA && buffer[26] == 0x0D && buffer[27] == 0x0A) {
                // 解析数据
                std::vector<uint8_t> frame(buffer.begin(), buffer.begin() + 28);
                parseData(frame);

                // 移除已解析的字节
                buffer.erase(buffer.begin(), buffer.begin() + 28);
            } else {
                // 无效的帧头，移除第一个字节并继续检查
                buffer.erase(buffer.begin());
            }
        }

        // 可选的延时，以防止过高的CPU占用率
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
// 线程函数，用于接收和解析数据
void receiveData() {
    while (true) {
        vector<uint8_t> response(0);  // 读取28个字节的数据
        my_serial->read(response, 28);
        
        // 十六进制打印收到的数据
        cout << "Received data: ";
        for (int i = 0; i < response.size(); i++) {
            cout << std::hex << (int)response[i] << " ";
        }
        cout << endl;

        // 解析数据
        parseData(response);

        // 延时以确保线程不会过于占用CPU
        // std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// 处理 Ctrl+C 信号的函数
void handleStopSignal(int signum) {
    // 向设备发送停止命令
    vector<uint8_t> stopCommand = {0x43, 0xAA, 0x0D, 0x0A};  // 停止命令
    if (my_serial->isOpen()) {
        my_serial->write(stopCommand);
        cout << "Stop command sent: 0x43 AA 0D 0A" << endl;
    }

    // 退出程序
    exit(signum);
}

int main() {
    // 串口设置
    string port = "/dev/ttyUSB0";
    unsigned long baud = 460800;
    my_serial = new serial::Serial(port, baud, serial::Timeout::simpleTimeout(1000));

    if (!my_serial->isOpen()) {
        cerr << "Failed to open serial port!" << endl;
        return 1;
    }
    cout << "Serial port opened successfully." << endl;

    // 注册信号处理器，捕获 Ctrl+C
    signal(SIGINT, handleStopSignal);

    // 创建线程接收数据
    std::thread receiver_thread(receiveDataBybyte);

    // 向设备发送启动指令
    vector<uint8_t> startCommand = {0x48, 0xAA, 0x0D, 0x0A};  // 0x49为开始符
    my_serial->write(startCommand);
    cout << "Start command sent." << endl;

    // 等待线程结束
    receiver_thread.join();

    // 清理资源
    delete my_serial;
    return 0;
}
