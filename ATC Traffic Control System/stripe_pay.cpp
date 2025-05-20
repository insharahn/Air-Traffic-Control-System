//AABIA ALI 23I-0704
//INSHARAH IRFAN 23I-0615
//CS-D

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <errno.h>

// Structure to store AVN data
struct AVN {
    std::string avn_id;
    std::string flight_id;
    std::string airline;
    std::string aircraft_type;
    std::string speed;
    std::string issuance_time;
    double fine_amount;
    std::string payment_status;
    std::string due_date;
};

int main() {
    std::vector<AVN> pending_avns;

    // Open FIFOs in current directory
    const char* stripe_fifo = "stripe_fifo";
    const char* portal_fifo = "portal_fifo";

    // Open stripe FIFO for reading (non-blocking)
    int stripe_fd = open(stripe_fifo, O_RDONLY | O_NONBLOCK);
    if (stripe_fd == -1) {
        std::cerr << "[StripePay] Failed to open stripe FIFO" << std::endl;
        return 1;
    }

    // Open portal FIFO for writing
    int portal_fd = open(portal_fifo, O_WRONLY);
    if (portal_fd == -1) {
        std::cerr << "[StripePay] Failed to open portal FIFO" << std::endl;
        close(stripe_fd);
        return 1;
    }

    // Set stdin to non-blocking
    int stdin_fd = fileno(stdin);
    int flags = fcntl(stdin_fd, F_GETFL, 0);
    fcntl(stdin_fd, F_SETFL, flags | O_NONBLOCK);

    // Main loop
    while (true) {
        // Use select to monitor stripe_fifo and stdin
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(stripe_fd, &read_fds);
        FD_SET(stdin_fd, &read_fds);

        int max_fd = std::max(stripe_fd, stdin_fd) + 1;
        struct timeval timeout;
        timeout.tv_sec = 1; // Check every 1 second
        timeout.tv_usec = 0;

        int ready = select(max_fd, &read_fds, nullptr, nullptr, &timeout);
        if (ready == -1) {
          //  std::cerr << "[StripePay] Select error: " << strerror(errno) << std::endl;
            break;
        }

        // Read available AVN messages from stripe_fifo
        if (FD_ISSET(stripe_fd, &read_fds)) {
            char buffer[512];
            while (true) {
                ssize_t n = read(stripe_fd, buffer, sizeof(buffer) - 1);
                if (n <= 0) break; // No more messages or error
                buffer[n] = '\0';
                std::string avn_msg = buffer;

                // Parse AVN message
                AVN avn;
                size_t pos = 0;
                std::string token;
                while (pos < avn_msg.length()) {
                    size_t next_pos = avn_msg.find(',', pos);
                    if (next_pos == std::string::npos) next_pos = avn_msg.length();
                    token = avn_msg.substr(pos, next_pos - pos);
                    pos = next_pos + 1;

                    if (token.find("AVN_ID=") == 0) avn.avn_id = token.substr(7);
                    else if (token.find("Flight=") == 0) avn.flight_id = token.substr(7);
                    else if (token.find("Airline=") == 0) avn.airline = token.substr(8);
                    else if (token.find("Type=") == 0) avn.aircraft_type = token.substr(5);
                    else if (token.find("Speed=") == 0) avn.speed = token.substr(6);
                    else if (token.find("Issued=") == 0) avn.issuance_time = token.substr(7);
                    else if (token.find("Fine=") == 0) avn.fine_amount = std::stod(token.substr(5));
                    else if (token.find("Status=") == 0) avn.payment_status = token.substr(7);
                    else if (token.find("Due=") == 0) avn.due_date = token.substr(4);
                }

                // Add to pending list if unpaid
                if (avn.payment_status == "unpaid") {
                    pending_avns.push_back(avn);
                    std::cout << "[StripePay] Received AVN: " << avn.avn_id << ", Flight=" << avn.flight_id << std::endl;
                }
            }
        }

        // Display pending AVNs if there are any
        if (!pending_avns.empty()) {
            std::cout << "\n[StripePay] Pending AVN Challans:" << std::endl;
            for (size_t i = 0; i < pending_avns.size(); ++i) {
                const AVN& avn = pending_avns[i];
                std::cout << "  " << i + 1 << ". AVN ID: " << avn.avn_id
                          << ", Flight: " << avn.flight_id
                          << ", Airline: " << avn.airline
                          << ", Type: " << avn.aircraft_type
                          << ", Fine: PKR " << avn.fine_amount
                          << ", Due: " << avn.due_date << std::endl;
            }
        } else {
            std::cout << "[StripePay] No pending AVNs." << std::endl;
        }

        // Check for user input from stdin
        if (FD_ISSET(stdin_fd, &read_fds)) {
            char input_buffer[256];
            ssize_t n = read(stdin_fd, input_buffer, sizeof(input_buffer) - 1);
            if (n > 0) {
                input_buffer[n] = '\0';
                std::string input = input_buffer;
                // Remove trailing newline
                if (!input.empty() && input.back() == '\n') {
                    input.pop_back();
                }

                int choice = 0;
                try {
                    choice = std::stoi(input);
                } catch (...) {
                    choice = 0;
                }

                if (!pending_avns.empty()) {
                    if (choice > 0 && choice <= static_cast<int>(pending_avns.size())) {
                        const AVN& selected_avn = pending_avns[choice - 1];
                        std::cout << "[StripePay] Processing payment for Flight=" << selected_avn.flight_id << std::endl;
                        sleep(3);

                        // Send confirmation
                        std::string confirmation = "AVN=" + selected_avn.avn_id + ",Status=paid\n";
                        write(portal_fd, confirmation.c_str(), confirmation.size());
                        std::cout << "[StripePay] Sent confirmation: " << confirmation;

                        // Remove from pending list
                        pending_avns.erase(pending_avns.begin() + (choice - 1));
                    } else if (choice == 0) {
                        std::cout << "[StripePay] Skipping payment." << std::endl;
                    } else {
                        std::cout << "[StripePay] Invalid choice, skipping." << std::endl;
                    }
                } else if (choice == 0) {
                    std::cout << "[StripePay] Exiting." << std::endl;
                    break;
                }
            }
        }

        // Prompt user periodically
        std::cout << "[StripePay] Enter the number of the AVN to pay (or 0 to skip/exit if no AVNs): ";

        // Brief pause to avoid busy loop
        usleep(100000); // 100ms
    }

    // Cleanup
    close(stripe_fd);
    close(portal_fd);

    return 0;
}
