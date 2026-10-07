void test_video_log(const char *tag, const char *format, ...);
#define ESP_LOGI(tag, ...) test_video_log(tag, __VA_ARGS__)
