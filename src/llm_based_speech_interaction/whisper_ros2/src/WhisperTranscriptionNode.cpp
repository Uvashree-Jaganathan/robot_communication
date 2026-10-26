#include <rclcpp/rclcpp.hpp>
#include <audio_common_msgs/msg/audio_data_stamped.hpp>
#include <audio_common_msgs/msg/audio_data.hpp>
#include <std_msgs/msg/string.hpp>
#include <whisper.h>
#include <vector>
#include <cstring>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>

class WhisperTranscriptionNode : public rclcpp::Node
{
public:
    WhisperTranscriptionNode() : Node("whisper_transcription_node")
    {
        // Declare parameters
        this->declare_parameter<int>("n_threads", 4);
        this->declare_parameter<std::string>("language", "en");
        this->declare_parameter<bool>("translate", false);
        this->declare_parameter<bool>("save_files", false);
        this->declare_parameter<std::string>("save_directory", "../../../recordings");
        this->declare_parameter<int>("sample_rate", 16000);
        this->declare_parameter<float>("volume_threshold", 0.01f);
        this->declare_parameter<std::string>("model_path", "/home/uva/Desktop/robot_communication/src/whisper.cpp/models/ggml-base.en.bin");

        // Get parameters
        std::string model_path = this->get_parameter("model_path").as_string();
        n_threads_ = this->get_parameter("n_threads").as_int();
        language_ = this->get_parameter("language").as_string();
        translate_ = this->get_parameter("translate").as_bool();
        save_files_ = this->get_parameter("save_files").as_bool();
        save_directory_ = this->get_parameter("save_directory").as_string();
        sample_rate_ = this->get_parameter("sample_rate").as_int();
        volume_threshold_ = this->get_parameter("volume_threshold").as_double();
        
        // Create save directory if it doesn't exist
        if (save_files_) {
            mkdir(save_directory_.c_str(), 0755);
            RCLCPP_INFO(this->get_logger(), "Saving recordings to: %s", save_directory_.c_str());
        }
        
        RCLCPP_INFO(this->get_logger(), "Loading Whisper model from: %s", model_path.c_str());
        
        // Use new whisper_init_from_file_with_params API
        whisper_context_params cparams = whisper_context_default_params();
        cparams.use_gpu = true;  // Set to true if there is GPU support
        
        ctx_ = whisper_init_from_file_with_params(model_path.c_str(), cparams);
        
        if (ctx_ == nullptr) {
            RCLCPP_ERROR(this->get_logger(), "Failed to load Whisper model!");
            rclcpp::shutdown();
            return;
        }
        
        RCLCPP_INFO(this->get_logger(), "Whisper model loaded successfully");
        
        transcription_pub_ = this->create_publisher<std_msgs::msg::String>("transcription", 10);

        // Create subscriber
        audio_sub_ = this->create_subscription<audio_common_msgs::msg::AudioData>(
            "/speech_audio", 10,
            std::bind(&WhisperTranscriptionNode::audioCallback, this, std::placeholders::_1));
    }
    
    ~WhisperTranscriptionNode()
    {
        if (ctx_ != nullptr) {
            whisper_free(ctx_);
        }
    }

private:
    std::string getTimestamp()
    {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t_now), "%Y%m%d_%H%M%S");
        ss << "_" << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }
    
    void saveWavFile(const std::string& filename, const std::vector<float>& pcm_data)
    {
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            RCLCPP_ERROR(this->get_logger(), "Failed to open file: %s", filename.c_str());
            return;
        }
        
        // WAV header
        uint32_t data_size = pcm_data.size() * 2;  // 16-bit samples
        uint32_t file_size = 36 + data_size;
        uint16_t num_channels = 1;
        uint32_t sample_rate = sample_rate_;
        uint16_t bits_per_sample = 16;
        uint32_t byte_rate = sample_rate * num_channels * bits_per_sample / 8;
        uint16_t block_align = num_channels * bits_per_sample / 8;
        
        // RIFF header
        file.write("RIFF", 4);
        file.write(reinterpret_cast<const char*>(&file_size), 4);
        file.write("WAVE", 4);
        
        // fmt chunk
        file.write("fmt ", 4);
        uint32_t fmt_size = 16;
        uint16_t audio_format = 1;  // PCM
        file.write(reinterpret_cast<const char*>(&fmt_size), 4);
        file.write(reinterpret_cast<const char*>(&audio_format), 2);
        file.write(reinterpret_cast<const char*>(&num_channels), 2);
        file.write(reinterpret_cast<const char*>(&sample_rate), 4);
        file.write(reinterpret_cast<const char*>(&byte_rate), 4);
        file.write(reinterpret_cast<const char*>(&block_align), 2);
        file.write(reinterpret_cast<const char*>(&bits_per_sample), 2);
        
        // data chunk
        file.write("data", 4);
        file.write(reinterpret_cast<const char*>(&data_size), 4);
        
        // Convert float32 back to int16 and write
        for (float sample : pcm_data) {
            int16_t int_sample = static_cast<int16_t>(sample * 32767.0f);
            file.write(reinterpret_cast<const char*>(&int_sample), 2);
        }
        
        file.close();
        //RCLCPP_INFO(this->get_logger(), "Saved audio to: %s", filename.c_str());
    }
    
    void saveTranscription(const std::string& filename, const std::string& text)
    {
        std::ofstream file(filename);
        if (!file.is_open()) {
            RCLCPP_ERROR(this->get_logger(), "Failed to open file: %s", filename.c_str());
            return;
        }
        
        file << text << std::endl;
        file.close();
        RCLCPP_INFO(this->get_logger(), "Saved transcription to: %s", filename.c_str());
    }
    
    void audioCallback(const audio_common_msgs::msg::AudioData::SharedPtr msg)
    {
        // 1. Convert incoming audio to float32
        size_t sample_count = msg->data.size() / 2;
        const int16_t* samples = reinterpret_cast<const int16_t*>(msg->data.data());
        
        float sum_squares = 0.0f;
        for (size_t i = 0; i < sample_count; i++) {
            float sample = static_cast<float>(samples[i]) / 32768.0f;
            audio_buffer_.push_back(sample);
            sum_squares += sample * sample;
        }
        
        // 2. Calculate current volume (RMS)
        float rms = std::sqrt(sum_squares / sample_count);

        // 3. State Machine Logic
        if (!is_speaking_) {
            // We are in SILENCE. If it gets loud, start recording!
            if (rms > volume_threshold_) {
                is_speaking_ = true;
                silence_counter_ = 0;
                RCLCPP_INFO(this->get_logger(), "Voice detected... listening");
            }
        } else {
            // We are in SPEAKING state.
            if (rms > volume_threshold_) {
                silence_counter_ = 0; // Reset timer because user is still talking
            } else {
                silence_counter_++; // Increment timer because it's quiet
            }

            // 4. The Trigger: If it's been quiet for too long, the sentence is finished
            if (silence_counter_ >= SILENCE_LIMIT) {
                RCLCPP_INFO(this->get_logger(), "Silence detected. Transcribing...");
                
                std::vector<float> pcm_to_process = audio_buffer_;
                audio_buffer_.clear(); 
                is_speaking_ = false;
                silence_counter_ = 0;

                auto start_time = std::chrono::steady_clock::now();
                std::string transcription = transcribe(pcm_to_process);
                auto end_time = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
                
                RCLCPP_INFO(this->get_logger(), "Whisper Processing Time: %ld ms", duration.count());
                
                if (!transcription.empty() && transcription != "[BLANK_AUDIO]") {
                    auto transcription_msg = std_msgs::msg::String();
                    transcription_msg.data = transcription;
                    transcription_pub_->publish(transcription_msg);
                    
                    RCLCPP_INFO(this->get_logger(), "========================================");
                    RCLCPP_INFO(this->get_logger(), "FINAL TRANSCRIPTION: %s", transcription.c_str());
                    RCLCPP_INFO(this->get_logger(), "========================================");
                }
            }
        }
    }
    
    std::string transcribe(const std::vector<float>& pcm_data)
    {
        // Set up whisper parameters
        whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        
        wparams.n_threads = n_threads_;
        wparams.print_progress = false;
        wparams.print_special = false;
        wparams.print_realtime = false;
        wparams.print_timestamps = false;
        wparams.translate = translate_;
        wparams.language = language_.c_str();
        wparams.no_context = true;
        wparams.single_segment = true;
        
        // Run inference
        if (whisper_full(ctx_, wparams, pcm_data.data(), pcm_data.size()) != 0) {
            RCLCPP_ERROR(this->get_logger(), "Failed to process audio");
            return "";
        }
        
        // Get transcription results
        std::string result;
        const int n_segments = whisper_full_n_segments(ctx_);
        
        for (int i = 0; i < n_segments; ++i) {
            const char* text = whisper_full_get_segment_text(ctx_, i);
            result += text;
        }
        
        // Trim whitespace
        size_t start = result.find_first_not_of(" \t\n\r");
        size_t end = result.find_last_not_of(" \t\n\r");
        
        if (start != std::string::npos && end != std::string::npos) {
            return result.substr(start, end - start + 1);
        }
        
        return result;
    }

private:
    std::vector<float> audio_buffer_;
    whisper_context* ctx_ = nullptr;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr transcription_pub_;
    rclcpp::Subscription<audio_common_msgs::msg::AudioData>::SharedPtr audio_sub_;
    int n_threads_;
    std::string language_;
    bool translate_;
    bool save_files_;
    std::string save_directory_;
    int sample_rate_;
    float volume_threshold_;
    bool is_speaking_ = false;
    int silence_counter_ = 0;
    const int SILENCE_LIMIT = 5; // 0.5 sec slient
};

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<WhisperTranscriptionNode>());
    rclcpp::shutdown();
    return 0;
}
