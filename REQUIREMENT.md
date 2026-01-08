Dưới đây là một bài tập C++ thú vị, vừa sức nhưng đủ thử thách để rèn kỹ năng I/O nhanh, xử lý dữ liệu lớn và tối ưu hiệu suất – rất hợp với vibe high-performance bạn đang build ở QuadGate I/O đấy!

### Bài tập: Fast Blob Indexer (C++ edition)

**Đề bài:**

Bạn cần viết chương trình C++ đọc một danh sách lớn các blob (binary large object) từ input, mỗi blob được mô tả bởi:

- Một chuỗi key (string, độ dài ≤ 50 ký tự, không chứa space)
- Một số nguyên 64-bit size (kích thước blob tính bằng byte)
- Một số nguyên 64-bit offset (vị trí bắt đầu của blob trong file dữ liệu lớn)

Input:
- Dòng đầu: số lượng blob N (1 ≤ N ≤ 10^6)
- N dòng tiếp theo: mỗi dòng format `key size offset` (cách nhau bởi space)

Yêu cầu:
1. Đọc toàn bộ input càng nhanh càng tốt.
2. Lưu trữ thông tin sao cho có thể tra cứu nhanh theo key.
3. Sau đó đọc Q truy vấn (Q ≤ 10^5):
   - Mỗi truy vấn là một key
   - Nếu key tồn tại → in ra `size offset`
   - Nếu không tồn tại → in ra `NOTFOUND`

**Input mẫu:**
```
5
avatar123 102400 0
document_pdf 512000 102400
thumbnail 8192 614400
video_mp4 10485760 622592
backup_zip 2097152 11108352
3
avatar123
not_exist
video_mp4
```

**Output mẫu:**
```
102400 0
NOTFOUND
10485760 622592
```

**Yêu cầu tối ưu:**
- Thời gian chạy dưới 1-2 giây với N=10^6, Q=10^5 (test trên máy thường).
- Bộ nhớ hợp lý (không vượt quá ~500MB).
- Dùng C++17 hoặc C++20.

**Gợi ý (không bắt buộc):**
- Dùng `unordered_map<string, pair<long long, long long>>` để lưu.
- Tối ưu đọc input nhanh: dùng `std::cin` với `std::ios::sync_with_stdio(false); cin.tie(NULL);` + đọc bằng `cin >>` hoặc tự write fast parser.
- Bonus challenge: Thử dùng `std::string_view` làm key trong unordered_map (cần custom hash nếu C++17).

**Bắt đầu nào!**  
ABC
