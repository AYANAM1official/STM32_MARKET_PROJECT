#include "protocol.h"

ProtocolManager_t g_protocol;

void Protocol_Init(void) {
    memset(&g_protocol, 0, sizeof(g_protocol));
}

// --- [关键] 中断调用的入队函数 ---
void Protocol_Receive_Byte_IRQ(uint8_t byte) {
    uint16_t next_head = (g_protocol.head + 1) % RING_BUFFER_SIZE;
    // 如果缓冲区未满，则存入
    if (next_head != g_protocol.tail) {
        g_protocol.ring_buf[g_protocol.head] = byte;
        g_protocol.head = next_head;
    }
}

// --- 内部工具：提取 Key:Value ---
static void Get_Value_By_Key(const char *line, const char *key, char *out_val, uint16_t max_len) {
    char *p = strstr(line, key);
    if (p) {
        p += strlen(key); // 跳过Key
        if (*p == ':') {
            p++; // 跳过冒号
            uint16_t i = 0;
            // 读取直到遇到逗号或换行
            while (*p != ',' && *p != '\0' && *p != '\r' && *p != '\n' && i < max_len - 1) {
                out_val[i++] = *p++;
            }
            out_val[i] = '\0';
        }
    } else {
        out_val[0] = '\0';
    }
}

// --- 主循环调用的解析函数 ---
uint8_t Protocol_Parse_Line(ParsedPacket_t *out_packet) {
    // 从 RingBuffer 取数据，尝试拼凑一行
    while (g_protocol.tail != g_protocol.head) {
        char ch = g_protocol.ring_buf[g_protocol.tail];
        g_protocol.tail = (g_protocol.tail + 1) % RING_BUFFER_SIZE;

        if (ch == '\n') { // 换行符，一行结束，开始解析
            g_protocol.line_buf[g_protocol.line_idx] = '\0';
            g_protocol.line_idx = 0;

            char temp_val[64];
            
            // 1. 识别 START
            if (strstr(g_protocol.line_buf, "CMD:SYNC_START")) {
                out_packet->event = EVENT_SYNC_START;
                Get_Value_By_Key(g_protocol.line_buf, "TOTAL", temp_val, 32);
                out_packet->total_count = atoi(temp_val);
                return 1;
            }
            // 2. 识别 DATA
            else if (strstr(g_protocol.line_buf, "CMD:SYNC_DATA")) {
                out_packet->event = EVENT_SYNC_DATA;
                
                Get_Value_By_Key(g_protocol.line_buf, "ID", temp_val, 32);
                out_packet->id = atoi(temp_val);
                
                Get_Value_By_Key(g_protocol.line_buf, "PR", temp_val, 32);
                out_packet->price = atof(temp_val);
                
                Get_Value_By_Key(g_protocol.line_buf, "NM", out_packet->name, 50);
                return 1;
            }
            // 3. 识别 END
            else if (strstr(g_protocol.line_buf, "CMD:SYNC_END")) {
                out_packet->event = EVENT_SYNC_END;
                Get_Value_By_Key(g_protocol.line_buf, "SUM", temp_val, 32);
                out_packet->total_count = atoi(temp_val);
                return 1;
            }
             // 4. 识别 SCAN
            else if (strstr(g_protocol.line_buf, "CMD:SCAN")) {
                out_packet->event = EVENT_SCAN;
                Get_Value_By_Key(g_protocol.line_buf, "ID", temp_val, 32);
                out_packet->id = atoi(temp_val);
                return 1;
            }
        } 
        else if (ch != '\r') {
            if (g_protocol.line_idx < LINE_BUFFER_SIZE - 1) {
                g_protocol.line_buf[g_protocol.line_idx++] = ch;
            }
        }
    }
    return 0; // 没拼凑出一整行
}
