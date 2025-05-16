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
#include <fstream>

using namespace std;

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

// To parse input from the logFile to get puranay AVNs
bool parse_avn(std::ifstream& file, AVN& avn) {
    std::string line;
    avn = AVN(); // Reset AVN
    while (std::getline(file, line)) {
        if (line.empty()) break; // End of AVN entry
        size_t eq_pos = line.find(" = ");
        if (eq_pos == std::string::npos) continue;
        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 3);

        if (key == "AVN_ID") avn.avn_id = value;
        else if (key == "Flight") avn.flight_id = value;
        else if (key == "Airline") avn.airline = value;
        else if (key == "Type") avn.aircraft_type = value;
        else if (key == "Speed") avn.speed = value;
        else if (key == "Issued") avn.issuance_time = value;
        else if (key == "Fine") avn.fine_amount = std::stod(value);
        else if (key == "Status") avn.payment_status = value;
        else if (key == "Due") avn.due_date = value;
    }
    return !avn.avn_id.empty();
}



int main() {
    vector<AVN> simAVNs;
    vector<AVN> avn_history;
    
	// Get all puranay AVNs from the log File
	std::ifstream logFileIn("AVNlog.txt");
	if (logFileIn.is_open()) {
		AVN avn;
		while (parse_avn(logFileIn, avn)) {
		    avn_history.push_back(avn);
		}
		logFileIn.close();
		std::cout << "[AVN Generator] Loaded " << avn_history.size() << " AVNs from AVNlog.txt" << std::endl;
	} else {
		std::cout << "[AVN Generator] No existing AVNlog.txt, starting fresh" << std::endl;
	}

    // Open portal FIFO for reading
    const char* portal_fifo = "portal_fifo";
int portal_fd = open(portal_fifo, O_RDONLY | O_NONBLOCK);
    if (portal_fd == -1) {
        std::cerr << "[Airline Portal] Failed to open portal FIFO" << std::endl;
        return 1;
    }

std::string flight_id, issuance_date;
while (true) {
    // Prompt for FlightID and issuance date
    std::cout << "\n[Airline Portal] Enter FlightID (or press Enter to exit): ";
    std::getline(std::cin, flight_id);
    if (flight_id.empty()) break; // Exit if FlightID is empty
    std::cout << "[Airline Portal] Enter AVN Issuance Date (YYYY-MM-DD): ";
    std::getline(std::cin, issuance_date);

    bool found = false;

    // Read available messages from FIFO
    char buffer[512];
    while (true) {
        ssize_t n = read(portal_fd, buffer, sizeof(buffer) - 1);
        if (n <= 0) break; // No more messages or error
        buffer[n] = '\0';
        std::string msg = buffer;

        // Check if it's an AVN or payment update
        if (msg.find("AVN_ID=") != std::string::npos) {
            // Parse AVN message
            AVN avn;
            size_t pos = 0;
            std::string token;
            while (pos < msg.length()) {
                size_t next_pos = msg.find(',', pos);
                if (next_pos == std::string::npos) next_pos = msg.length();
                token = msg.substr(pos, next_pos - pos);
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

            // Store in history
            avn_history.push_back(avn);

            // Check if matches user input
            if (avn.flight_id == flight_id && avn.issuance_time.substr(0, 10) == issuance_date) { // to check against date only not time
                found = true;
                std::cout << "[Airline Portal] Matching AVN Found:" << std::endl;
                std::cout << "  AVN ID: " << avn.avn_id << std::endl
                          << "  Flight: " << avn.flight_id << std::endl
                          << "  Airline: " << avn.airline << std::endl
                          << "  Aircraft Type: " << avn.aircraft_type << std::endl
                          << "  Speed (Recorded/Permissible): " << avn.speed << std::endl
                          << "  Issuance Time: " << avn.issuance_time << std::endl
                          << "  Fine Amount: PKR " << avn.fine_amount << std::endl
                          << "  Payment Status: " << avn.payment_status << std::endl
                          << "  Due Date: " << avn.due_date << std::endl;
            }
        } else if (msg.find("AVN=") != std::string::npos && msg.find("Status=paid") != std::string::npos) {
    // Parse payment update
    std::string paid_avn_id;
    size_t avn_pos = msg.find("AVN=");
    if (avn_pos != std::string::npos) {
        size_t comma_pos = msg.find(',', avn_pos);
        if (comma_pos != std::string::npos) {
            paid_avn_id = msg.substr(avn_pos + 4, comma_pos - (avn_pos + 4));
        }
    }
    // Update payment status in history
    bool found = false;
    for (auto& avn : avn_history) {
        if (avn.avn_id == paid_avn_id) {
            avn.payment_status = "paid";
            found = true;
            std::cout << "[Airline Portal] Payment confirmed for AVN=" << paid_avn_id << std::endl;
            std::cout << "  AVN ID: " << avn.avn_id << std::endl
                      << "  Flight: " << avn.flight_id << std::endl
                      << "  Airline: " << avn.airline << std::endl
                      << "  Type: " << avn.aircraft_type << std::endl
                      << "  Speed: " << avn.speed << std::endl
                      << "  Issued: " << avn.issuance_time << std::endl
                      << "  Fine: PKR " << avn.fine_amount << std::endl
                      << "  Status: " << avn.payment_status << std::endl
                      << "  Due: " << avn.due_date << std::endl;
            break; // Update only the matching AVN
        }
    }
    if (!found) {
        std::cout << "[Airline Portal] No AVN found for AVN=" << paid_avn_id << std::endl;
    }
    // Check if the matching AVN is now paid
    if (found) {
        for (const auto& avn : avn_history) {
            if (avn.avn_id == paid_avn_id && avn.flight_id == flight_id && avn.issuance_time.substr(0, 10) == issuance_date) {
                std::cout << "[Airline Portal] Updated Matching AVN (now paid):" << std::endl;
                std::cout << "  AVN ID: " << avn.avn_id << std::endl
                          << "  Flight: " << avn.flight_id << std::endl
                          << "  Airline: " << avn.airline << std::endl
                          << "  Type: " << avn.aircraft_type << std::endl
                          << "  Speed: " << avn.speed << std::endl
                          << "  Issued: " << avn.issuance_time << std::endl
                          << "  Fine: PKR " << avn.fine_amount << std::endl
                          << "  Status: " << avn.payment_status << std::endl
                          << "  Due: " << avn.due_date << std::endl;
                break;
            }
        }
    }
} else {
            std::cerr << "[Airline Portal] Invalid message: " << msg << std::endl;
        }
    }

    // Display history for FlightID
    std::cout << "\n[Airline Portal] AVN History for FlightID=" << flight_id << ":" << std::endl;
    bool has_history = false;
    for (const auto& avn : avn_history) {
        if (avn.flight_id == flight_id) {
            has_history = true;
            std::cout << "  - AVN ID: " << avn.avn_id
                      << ", Issued: " << avn.issuance_time
                      << ", Fine: PKR " << avn.fine_amount
                      << ", Status: " << avn.payment_status
                      << ", Due: " << avn.due_date << std::endl;
        }
    }
    if (!has_history) {
        std::cout << "  No AVNs found for FlightID=" << flight_id << std::endl;
    }

    if (!found) {
        std::cout << "[Airline Portal] No AVN found for FlightID=" << flight_id
                  << " and Issuance Date=" << issuance_date << std::endl;
    }
}

// Cleanup
close(portal_fd);
return 0;

}



