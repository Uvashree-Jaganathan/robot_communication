#include <chrono>
#include <memory>
#include <string>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

using namespace std::chrono_literals;

class PubSubNode : public rclcpp::Node {
public:
  PubSubNode() : Node("llm_node"), count_(0) {
    // Create Publisher
    publisher_ = this->create_publisher<std_msgs::msg::String>("/llm_response", 10);

    // Create Subscriber
    subscription_ = this->create_subscription<std_msgs::msg::String>(
        "/transcription", 10,
        std::bind(&PubSubNode::listener_callback, this, std::placeholders::_1));
  }

private:

  static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
  {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
  }

  std::string call_openai_api(const std::string& prompt) {
    CURL* curl = curl_easy_init();
    if (!curl) {
      RCLCPP_ERROR(this->get_logger(), "Failed to initialize CURL");
      return "Error: CURL init failed";
    }

    // Get API Key from environment variable for security
    const char* api_key = std::getenv("OPENAI_API_KEY");
    if (!api_key) {
      RCLCPP_ERROR(this->get_logger(), "OPENAI_API_KEY environment variable not set!");
      curl_easy_cleanup(curl);
      return "Error: API Key not found";
    }

    std::string readBuffer;
    std::string url = "https://api.openai.com/v1/chat/completions";
    
    // OpenAI uses a messages array for chat history
    std::string system_prompt = "You are a helpful, friendly robot assistant. Keep your response very short and concise (1-2 sentences). Do not list your capabilities.";
    nlohmann::json payload = {
        {"model", "gpt-4o"},
        {"messages", {
            {{"role", "system"}, {"content", system_prompt}},
            {{"role", "user"}, {"content", prompt}}
        }},
        {"stream", false}
    };
    std::string json_payload = payload.dump();

    struct curl_slist* headers = NULL;
    std::string auth_header = "Authorization: Bearer " + std::string(api_key);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
      RCLCPP_ERROR(this->get_logger(), "CURL request failed: %s", curl_easy_strerror(res));
      readBuffer = "Error: API request failed";
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
     try {
      auto j = nlohmann::json::parse(readBuffer);
      // OpenAI response structure: choices[0].message.content
      return j.at("choices")[0].at("message").at("content").get<std::string>();
    } catch (const std::exception& e) {
      RCLCPP_ERROR(this->get_logger(), "JSON Parsing failed: %s", e.what());
      return "Error: Could not parse OpenAI response";
    }
  }

  void listener_callback(const std_msgs::msg::String::SharedPtr msg) {
    RCLCPP_INFO(this->get_logger(), "I heard: '%s'", msg->data.c_str());
    
    if(is_speaking_)
    {
      RCLCPP_INFO(this->get_logger(), "Ignoring input while speaking.");
      return;
    }

    std::string current_text = msg->data;

    // Cumulative Filter: If the new message starts with the last message, remove the old part
    if (!last_message_.empty() && current_text.find(last_message_) == 0) {
      RCLCPP_INFO(this->get_logger(), "Cumulative text detected. Filtering...");
      current_text = current_text.substr(last_message_.length());
      
      // Trim leading spaces, punctuation, or " "
      size_t first_char = current_text.find_first_not_of(" ,.?!");
      if (first_char != std::string::npos) {
        current_text = current_text.substr(first_char);
      } else {
        // If nothing is left after filtering, it's a duplicate
        RCLCPP_INFO(this->get_logger(), "Duplicate message ignored.");
        return;
      }
    }

    if(current_text == last_message_)
    {
      RCLCPP_INFO(this->get_logger(), "Duplicate message ignored.");
      return;
    }
    last_message_ = current_text;

    //call openai api
    auto start_time = std::chrono::steady_clock::now();
    std::string llm_response = call_openai_api(current_text);
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    RCLCPP_INFO(this->get_logger(), "LLM Processing Time: %ld ms", duration.count());
    RCLCPP_INFO(this->get_logger(), "LLM Response: %s", llm_response.c_str());

    //create a response message
    auto response = std_msgs::msg::String();
    response.data = llm_response;
    
    //publish it
    publisher_->publish(response);

    is_speaking_ = true;
    speaking_timer_ = this->create_wall_timer
    (
      5s, [this]()
      {
        this->is_speaking_ = false;
        this->speaking_timer_->cancel();
        RCLCPP_INFO(this->get_logger(), "Ready to listen again.");
      }
    );
  }

  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_;
  std::string last_message_;
  bool is_speaking_ = false;
  rclcpp::TimerBase::SharedPtr speaking_timer_;
  size_t count_;
};

int main(int argc, char *argv[]) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<PubSubNode>());
  rclcpp::shutdown();
  return 0;
}
