# **Product Requirements Document (PRD) & MVP Blueprint**

## **Project Name: Personal Binaural & Spatial Audio Render Engine**

**Target AI Developer:** Claude Code / Anti Gravity / AI Coding Assistant

### **1\. Project Overview (Tổng quan dự án)**

Tôi cần phát triển một phần mềm xử lý âm thanh không gian (Spatial Audio Downmixer) hoạt động trên nền tảng Desktop (Windows/macOS).  
Khác với các giải pháp thương mại "hộp đen" (như Dolby Atmos for Headphones, Apple Spatial Audio) hay các script tự động (như dự án của peqdb/macos), dự án này hướng đến việc trao quyền kiểm soát 100% **Impulse Response (IR / HRIR / PRIR)** cho người dùng.  
**Mục tiêu cốt lõi:** Lấy tín hiệu Multichannel (5.1 / 7.1 / Dolby Atmos bed channels) và áp dụng phép tích chập (Convolution) với các file HRIR tùy chỉnh để render ra tín hiệu Stereo 2.0 (Binaural) dành cho tai nghe In-Ear Monitor (IEM) hoặc Headphone, bảo toàn tối đa âm trường và giảm thiểu suy hao chi tiết (Comb-filtering).

### **2\. MVP Scope (Phạm vi của Minimum Viable Product)**

Ở giai đoạn MVP, chúng ta sẽ **không** nhúng thẳng vào driver hệ điều hành (không làm Virtual Audio Device ngay). Thay vào đó, MVP sẽ hoạt động dưới dạng một ứng dụng Offline Renderer hoặc VST/AU Plugin.  
**Tính năng MVP:**

1. **Input:** Chấp nhận file âm thanh đầu vào có nhiều kênh (Multichannel .wav, .flac) hoặc trích xuất luồng âm thanh từ file video (.mkv, .mp4).  
2. **Channel Mapping (Routing):** Tự động nhận diện và phân tách các kênh (L, R, C, LFE, SL, SR, BL, BR, v.v.).  
3. **HRIR Convolution Engine:** Lõi xử lý bằng thuật toán Fast Fourier Transform (FFT Convolution). Người dùng chỉ định một thư mục chứa các file .wav HRIR/PRIR tương ứng cho từng kênh.  
4. **Mixdown & Output:** Tổng hợp các tín hiệu đã xử lý thành 1 file Stereo (Binaural) xuất ra với chất lượng cao (32-bit float hoặc 24-bit).

### **3\. Technical Architecture (Kiến trúc kỹ thuật đề xuất)**

*Claude, please analyze this and suggest the best stack. My initial thought is:*

* **Core Audio Engine:** Python (với numpy, scipy.signal.fftconvolve, soundfile). Python phù hợp để dựng MVP nhanh chóng cho phần xử lý tín hiệu DSP.  
* **Media Handling (Demuxing/Decoding):** Sử dụng FFmpeg wrapper (ví dụ: ffmpeg-python) để bóc tách luồng âm thanh 5.1/7.1 từ file video (MKV/MP4) trước khi đưa vào Python xử lý.  
* **GUI (Optional for MVP, but good for testing):** Thư viện nhẹ như CustomTkinter hoặc PyQt để người dùng chọn file Input và folder chứa HRIR.

### **4\. Logic Xử Lý (Core Processing Logic)**

*Claude, here is the pseudocode/logic flow I want to implement:*

1. **Extract:** Dùng FFmpeg đọc file movie.mkv (có track âm thanh 5.1). Trích xuất track đó ra thành ma trận numpy N x 6 (N: số samples, 6: số kênh).  
2. **HRIR Loading:** Nạp các file HRIR từ thư mục do người dùng chọn. Lưu ý: Mỗi kênh loa ảo (vd: Center) phải cần 2 file HRIR (hoặc 1 file stereo IR) để mô phỏng thời gian truyền âm đến Tai Trái (ITD) và Tai Phải.  
   * *Ví dụ:* Loa Center \-\> C\_to\_Left.wav, C\_to\_Right.wav  
   * *Ví dụ:* Loa Surround Left \-\> SL\_to\_Left.wav, SL\_to\_Right.wav  
3. **Convolution Phase (The Heavy Lifting):**  
   * Khởi tạo 2 mảng Output\_Left \= 0 và Output\_Right \= 0\.  
   * Lặp qua từng kênh đầu vào (từ 1 đến 6).  
   * Conv\_Left \= fftconvolve(Channel\_Data, HRIR\_to\_Left)  
   * Conv\_Right \= fftconvolve(Channel\_Data, HRIR\_to\_Right)  
   * Output\_Left \+= Conv\_Left  
   * Output\_Right \+= Conv\_Right  
4. **Normalization:** Chuẩn hóa âm lượng tín hiệu Output\_Left và Output\_Right để tránh Clipping, nhưng phải giữ nguyên dải động (Dynamic Range). Tùy chọn thêm bộ lọc LPF cho kênh LFE (Subwoofer) nếu cần.  
5. **Export:** Ghi ra file output\_binaural.wav.

### **5\. Những vấn đề cần Claude giải quyết (Known Challenges)**

Khi code dự án này, hãy đặc biệt chú ý và đề xuất giải pháp cho các vấn đề sau:

* **Hiệu năng FFT:** Tích chập file phim dài 2 tiếng với HRIR sẽ ngốn rất nhiều RAM nếu nạp toàn bộ vào bộ nhớ. Cần thuật toán xử lý theo từng khối (Block convolution / Overlap-Add method).  
* **Dolby Atmos (Object-based audio):** Tạm thời trong MVP, chúng ta xử lý Atmos ở dạng Bed Channels (7.1.4 hoặc 5.1.2) tức là downmix từ tín hiệu tĩnh. Việc giải mã metadata của Atmos object-based rất phức tạp vì nó bị khóa bản quyền mã hóa. *Claude, hãy tư vấn cách dùng FFmpeg để đọc track TrueHD Atmos và lấy ra 7.1 channel bed.*  
* **LFE Channel Routing:** Kênh LFE (loa siêu trầm) thường không cần chạy qua HRIR (vì âm trầm đa hướng), nhưng cần mix trực tiếp vào 2 kênh L và R với một bộ lọc cắt tần (Low Pass Filter \~ 120Hz).

### **6\. User Story (Trải nghiệm người dùng mong đợi)**

"Tôi vừa tải một file phim MKV có chuẩn âm thanh TrueHD 7.1. Tôi đã có sẵn một bộ thư mục chứa các file HRIR đo từ rạp phim IMAX. Tôi mở tool lên, kéo thả file MKV vào, trỏ đường dẫn đến thư mục IMAX HRIR, bấm Render. 10 phút sau, tôi nhận được file movie\_binaural.mkv (đã thay thế track âm thanh cũ). Tôi cắm chiếc tai nghe IEM của mình vào điện thoại, xem file phim đó và có cảm giác như đang ngồi giữa rạp IMAX."  
**Lời nhắn cho Claude:** Hãy đọc kỹ PRD này. Đầu tiên, hãy xác nhận xem bạn đã hiểu rõ kiến trúc và giới hạn vật lý của Binaural Downmix chưa. Sau đó, hãy cung cấp cho tôi **cấu trúc thư mục dự án (Project Structure)** và **đoạn code của engine.py xử lý Overlap-Add FFT Convolution** trước nhé.