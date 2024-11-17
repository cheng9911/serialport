#include <iostream>
#include <iomanip>
#include <vector>
#include <cstring>
#include <thread>
#include <signal.h>
#include <serial/serial.h>
#define BUFFER_SIZE 256
#define FRAME_HEADER 0x48  // 帧头
#define FRAME_TAIL 0x0A    // 帧尾

// 定义帧的开头和结尾字节
const uint8_t FRAME_HEADER_1 = 0x48;
const uint8_t FRAME_HEADER_2 = 0xAA;
const uint8_t FRAME_TAIL_1 = 0x0D;
const uint8_t FRAME_TAIL_2 = 0x0A;
using namespace std;

serial::Serial* my_serial = nullptr;  // 定义全局串口对象
// 环形缓冲区结构体
struct CircularBuffer {
    uint8_t buffer[BUFFER_SIZE];  // 缓冲区
    uint8_t get_ptr = 0;          // 数据读取指针
    uint8_t recv_ptr = 0;         // 数据写入指针
    uint8_t start_ptr = 0;        // 当前帧的起始指针
    uint8_t end_ptr = 0;          // 当前帧的结束指针
    bool is_parsing_frame = false; // 标记是否检测到帧头
};

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
void receiveDataByPtr() {
    std::vector<uint8_t> buffer(28 * 5);  // 缓冲区定义为 28*5
    int head = 0;  // 起始指针
    int tail = 0;  // 结束指针
    const int frame_size = 28;
    
    while (true) {
        // 每次读取64字节并放入缓冲区
        std::vector<uint8_t> temp(64);
        int bytesRead = my_serial->read(temp.data(), 64);

        // 将读取的数据复制到缓冲区
        buffer.insert(buffer.end(), temp.begin(), temp.begin() + bytesRead);

        // 检查是否有足够的字节形成完整帧
        while (tail <= buffer.size() - frame_size) {
            // 验证帧头和帧尾
            if ((buffer[head] == 0x48 || buffer[head] == 0x49) && buffer[head + 1] == 0xAA && buffer[head + 26] == 0x0D && buffer[head + 27] == 0x0A) {
                // 提取并解析28字节帧
                std::vector<uint8_t> frame(buffer.begin() + head, buffer.begin() + head + frame_size);
                parseData(frame);
                
                // 更新指针位置，跳过已解析的数据
                head += frame_size;
                tail = head;
            } else {
                // 如果不是有效帧，头指针移位1字节，重新定位
                head += 1;
                tail = head;
            }
        }

        // 清除已处理的数据，防止缓冲区过长
        if (head > 0) {
            buffer.erase(buffer.begin(), buffer.begin() + head);
            head = 0;
            tail = 0;
        }

        // CPU负载保护
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
// 接收数据并处理环形缓冲区中的数据
void receiveAndProcessData(CircularBuffer& cb) {
    // 接收数据到环形缓冲区
    vector<uint8_t> response;  // 动态读取数据
    my_serial->read(response, 28);  // 读取最多28个字节的数据
    size_t byte_count = response.size();

    for (size_t i = 0; i < byte_count; ++i) {
        cb.buffer[cb.recv_ptr] = response[i];
        cb.recv_ptr = (cb.recv_ptr + 1) & 0xFF;  // 更新 recv_ptr 指针
    }

    // 处理缓冲区中的数据
    while (cb.get_ptr != cb.recv_ptr) {  // 检查缓冲区是否有未处理的数据
        uint8_t current_byte = cb.buffer[cb.get_ptr];
        cb.get_ptr = (cb.get_ptr + 1) & 0xFF;  // 环形缓冲区，更新 get_ptr 指针

        if (current_byte == FRAME_HEADER) {
            // 找到帧头，设置起始指针
            cb.start_ptr = cb.get_ptr;
            cb.is_parsing_frame = true;
        } else if (cb.is_parsing_frame && current_byte == FRAME_TAIL) {
            // 找到帧尾，设置结束指针
            cb.end_ptr = cb.get_ptr;
            cb.is_parsing_frame = false;  // 关闭帧标志

            // 计算帧长度
            size_t frame_length = (cb.end_ptr >= cb.start_ptr) 
                                  ? (cb.end_ptr - cb.start_ptr) 
                                  : (BUFFER_SIZE - cb.start_ptr + cb.end_ptr);

            if (frame_length != 28) {
                // 如果帧长度不是28字节，丢弃该帧
                cout << "Invalid frame length: " << frame_length << ". Frame discarded." << endl;
                continue;
            }

            // 提取帧数据
            vector<uint8_t> frame(frame_length);
            for (size_t i = 0; i < frame_length; ++i) {
                frame[i] = cb.buffer[(cb.start_ptr + i) & 0xFF];
            }

            // 调用解析函数
            parseData(frame);
        }
    }
}


// 接收数据并处理环形缓冲区中的数据
void receiveAndProcessDataByHeader2(CircularBuffer& cb) {
    // 接收数据到环形缓冲区
    vector<uint8_t> response(0);  // 读取28个字节的数据
    my_serial->read(response, 28);
    size_t count = response.size();
    
    for (size_t i = 0; i < count; ++i) {
        cb.buffer[cb.recv_ptr] = response[i];
        cb.recv_ptr = (cb.recv_ptr + 1) & 0xFF;  // 环形缓冲区
    }

    // 处理缓冲区中的数据
    while (cb.get_ptr != cb.recv_ptr) {  // 如果缓冲区有未处理的数据
        uint8_t current_byte = cb.buffer[cb.get_ptr];
        cb.get_ptr = (cb.get_ptr + 1) & 0xFF;  // 环形缓冲区

        if (cb.is_parsing_frame) {
            if (current_byte == FRAME_TAIL_1) {
                // 找到第一个结束字节
                uint8_t next_byte = cb.buffer[cb.get_ptr];
                if (next_byte == FRAME_TAIL_2) {
                    // 找到帧尾，设置结束指针
                    cb.end_ptr = cb.get_ptr;
                    cb.is_parsing_frame = false;  // 关闭帧标志

                    // 计算帧长度
                    size_t length = (cb.end_ptr >= cb.start_ptr) ? 
                                    (cb.end_ptr - cb.start_ptr) : 
                                    (BUFFER_SIZE - cb.start_ptr + cb.end_ptr);

                    if (length != 28) {
                        // 如果帧长度不是28字节，放弃该帧
                        cout << "Invalid frame length: " << length << ". Frame discarded." << endl;
                        continue;
                    }

                    // 提取帧数据
                    vector<uint8_t> frame(length);
                    for (size_t i = 0; i < length; ++i) {
                        frame[i] = cb.buffer[(cb.start_ptr + i) & 0xFF];
                    }

                    // 调用解析函数
                    parseData(frame);
                }
            }
        } else if (current_byte == FRAME_HEADER_1) {
            // 找到第一个帧头字节
            uint8_t next_byte = cb.buffer[cb.get_ptr];
            if (next_byte == FRAME_HEADER_2) {
               // 找到完整的帧头，设置起始指针为前一个字节位置
                cb.start_ptr = (cb.get_ptr == 0) ? (BUFFER_SIZE - 1) : (cb.get_ptr - 1);
                cb.is_parsing_frame = true;
            }
        }
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
