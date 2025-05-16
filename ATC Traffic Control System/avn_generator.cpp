//AABIA ALI 23I-0704
//INSHARAH IRFAN 23I-0615
//CS-D

#include <iostream>
#include <unordered_map>
#include <string>
#include <sstream>
#include <ctime>
#include <iomanip>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fstream>

using namespace std;

    
// AVN structure
struct AVN {
    std::string avn_id;           // e.g., AVN001
    std::string airline_name;     // e.g., PIA
    std::string flight_number;    // e.g., PIA001
    std::string aircraft_type;    // Commercial, Cargo, Emergency
    double speed_recorded;        // Speed at violation
    double speed_permissibleMIN;     // Min allowed speed    
    double speed_permissibleMAX;     // Max allowed speed
    std::string issuance_time;    // YYYY-MM-DD HH:MM:SS
    double fine_amount;           // Total with 15% fee
    std::string payment_status;   // unpaid, paid
    std::string due_date;         // 3 days from issuance
};

// Generate unique AVN ID
std::string generate_avn_id(int count) {
    return "AVN" + std::string(3 - std::to_string(count).length(), '0') + std::to_string(count);
}

// Get current time as string
std::string get_current_time() {
    std::time_t now = std::time(nullptr);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// Get due date (3 days from now)
std::string get_due_date() {
    std::time_t now = std::time(nullptr);
    now += 3 * 24 * 60 * 60; // Add 3 days
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now), "%Y-%m-%d");
    return ss.str();
}

// Calculate fine amount
double calculate_fine(const std::string& aircraft_type) {
    double base_fine = (aircraft_type == "Commercial" || aircraft_type == "Emergency") ? 500000.0 : 700000.0;
    return base_fine * 1.15; // Add 15% service fee
}


int main() {


    ofstream logFile;
    logFile.open("AVNlog.txt", ios::app | ios::out); // changed  to append as well
        if (!logFile.is_open()) {
            cerr << "Failed to open log file!" << endl;
            exit(1); // Exit if log file can't be opened
        }


    std::unordered_map<std::string, AVN> avn_map;

    
    int avn_count = 1;

    // Create FIFOs
    const char* portal_fifo = "portal_fifo";
    const char* stripe_fifo = "stripe_fifo";
    const char* payment_fifo = "payment_fifo";
    mkfifo(portal_fifo, 0666);
    mkfifo(stripe_fifo, 0666);
    mkfifo(payment_fifo, 0666);

    // Open payment FIFO for reading confirmations
    int payment_fd = open(payment_fifo, O_RDONLY | O_NONBLOCK);
    if (payment_fd == -1) {
        std::cerr << "Failed to open payment FIFO" << std::endl;
        return 1;
    }

    // Read flight data from terminal
    /*
    std::string line;
    std::cout << "Enter flight data (e.g., PIA001 PIA 0 650 HOLDING 600)\n";
    std::cout << "Format: FlightID Airline Type(0=Commercial,1=Cargo,2=Emergency) Speed Phase PermissibleSpeedMIN PermissibleSpeedMAX\n";
    std::cout << "Press Enter twice or Ctrl+D to finish:\n";
    */
    
   // Read flight data from stdin (piped from ATC)
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            std::cerr << "[AVN Generator] Empty input, skipping" << std::endl;
            continue;
        }
        
        // Parse input (e.g., "PIA001 PIA 0 650 HOLDING 200 600")
        std::stringstream ss(line);
        std::string flight_id, airline, type_str, phase;
        double speed, permissiblemin, permissiblemax;
        bool valid = true;

        // Read space-separated fields
        if (!(ss >> flight_id)) valid = false;
        if (!(ss >> airline)) valid = false;
        if (!(ss >> type_str)) valid = false;
        if (!(ss >> speed)) valid = false;
        if (!(ss >> phase)) valid = false;
        if (!(ss >> permissiblemin)) valid = false;
        if (!(ss >> permissiblemax)) valid = false;

        if (!valid || !ss.eof()) { // Check for extra fields
            std::cerr << "[AVN Generator] Invalid input: " << line << std::endl;
            continue;
        }

        // Validate type
        std::string aircraft_type;
        if (type_str == "0") aircraft_type = "Commercial";
        else if (type_str == "1") aircraft_type = "Cargo";
        else if (type_str == "2") aircraft_type = "Emergency";
        else {
            std::cerr << "[AVN Generator] Invalid aircraft type: " << type_str << std::endl;
            continue;
        }

        // Generate AVN
        AVN avn;
        avn.avn_id = generate_avn_id(avn_count++);
        avn.airline_name = airline;
        avn.flight_number = flight_id;
        avn.aircraft_type = aircraft_type;
        avn.speed_recorded = speed;
        avn.speed_permissibleMIN = permissiblemin;
        avn.speed_permissibleMAX = permissiblemax;
        avn.issuance_time = get_current_time();
        avn.fine_amount = calculate_fine(aircraft_type);
        avn.payment_status = "unpaid";
        avn.due_date = get_due_date();

        // Store in hashmap
        // maps an AVN to a flight id
        avn_map[flight_id] = avn;

        // Prepare AVN message
        
        // ye wala is for sending through pipes again, \n nahi hai is main
        string avn_msg = "AVN_ID=" + avn.avn_id + ",Flight=" + avn.flight_number +
                             ",Airline=" + avn.airline_name + ",Type=" + avn.aircraft_type +
                             ",Speed=" + std::to_string(avn.speed_recorded) + "/" +
                             std::to_string(avn.speed_permissibleMIN) + " - " +  std::to_string(avn.speed_permissibleMAX)  + ",Issued=" + avn.issuance_time +
                             ",Fine=" + std::to_string(avn.fine_amount) + ",Status=" + avn.payment_status +
                             ",Due=" + avn.due_date + "\n";
                   
        // ye log files ke liye hai, just cuz it's easier to read   
        string log_msg = "AVN_ID = " + avn.avn_id + "\n" + "Flight = " + avn.flight_number + "\n" +
                             "Airline = " + avn.airline_name + "\n" + "Type = " + avn.aircraft_type + "\n"+
                             "Speed = " + std::to_string(avn.speed_recorded) + "\n"  + "Permissible range = " +
                             std::to_string(avn.speed_permissibleMIN) + " - " +  std::to_string(avn.speed_permissibleMAX) + "\n" + "Issued = " + avn.issuance_time +"\n"  				+ "Fine = " + std::to_string(avn.fine_amount) + "\n"+ "Status = " + avn.payment_status + "\n" +
                             "Due = " + avn.due_date + "\n";
                             
                                     logFile << log_msg << endl;

        // Send to Airline Portal FIFO
        int portal_fd = open(portal_fifo, O_WRONLY | O_NONBLOCK);
        if (portal_fd != -1) {
            write(portal_fd, avn_msg.c_str(), avn_msg.size());
            close(portal_fd);
            std::cout << "[AVN Generator] Sent to Portal: " << avn_msg;
        } else {
            std::cout << "[AVN Generator] Portal FIFO not available, logged: " << avn_msg;
        }

        // Send to StripePay FIFO
        int stripe_fd = open(stripe_fifo, O_WRONLY | O_NONBLOCK);
        if (stripe_fd != -1) {
            write(stripe_fd, avn_msg.c_str(), avn_msg.size());
            close(stripe_fd);
            std::cout << "[AVN Generator] Sent to StripePay: " << avn_msg;
        } else {
            std::cout << "[AVN Generator] StripePay FIFO not available, logged: " << avn_msg;
        }

        // Check for payment confirmations
        // Flight=123,Status=paid
        char buffer[256];
        ssize_t n = read(payment_fd, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';
            std::string confirmation = buffer;
            size_t flight_pos = confirmation.find("Flight=");
            if (flight_pos != std::string::npos) {
                size_t status_pos = confirmation.find(",Status=");
                if (status_pos != std::string::npos) {
                    std::string flight_id = confirmation.substr(7, status_pos - 7);
                    if (avn_map.find(flight_id) != avn_map.end()) {
                        avn_map[flight_id].payment_status = "paid";
                        std::cout << "[AVN Generator] Updated " << flight_id << " to paid" << std::endl;

                        // Notify Airline Portal
                        std::string update_msg = "Flight=" + flight_id + ",Status=paid\n";
                        portal_fd = open(portal_fifo, O_WRONLY | O_NONBLOCK);
                        if (portal_fd != -1) {
                            write(portal_fd, update_msg.c_str(), update_msg.size());
                            close(portal_fd);
                            std::cout << "[AVN Generator] Notified Portal: " << update_msg;
                        }

                    }
                }
            }
        }
    }

    // Cleanup
    close(payment_fd);
    unlink(portal_fifo);
    unlink(stripe_fifo);
    unlink(payment_fifo);




    /*// Print final AVN map
    std::cout << "[AVN Generator] Final AVN Map:" << std::endl;
    for (const auto& pair : avn_map) {
        const AVN& avn = pair.second;
        cout << "FlightID = " << avn.flight_number << " || AVN_ID = " << avn.avn_id
                  << " || Status= " << avn.payment_status<< " || Type = " << avn.aircraft_type<< " || Fine = " << avn.fine_amount << std::endl;
    }
    */

    return 0;
}





