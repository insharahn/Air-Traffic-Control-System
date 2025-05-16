// Insharah Irfan :   23i-0615
// Aabia Ali  :   23i-0704
// CS - D
// OPERATING SYSTEMS FINAL PROJECT MODULE #2

#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <atomic>
#include <algorithm>
#include <ctime>
#include <cstdlib>
#include <array>

using namespace std;

// Constants
const int MAX_ACTIVE_FLIGHTS = 20;
const int MAX_RUNWAYS = 3;
const int SIMULATION_DURATION = 300; // 5 minutes in seconds
const double LOW_FUEL_THRESHOLD = 20.0; // 20%

// Enums
enum AircraftType { COMMERCIAL, CARGO, EMERGENCY };
enum FlightPhase { HOLDING, APPROACH, LANDING, TAXI, AT_GATE, TAKEOFF_ROLL, CLIMB, CRUISE };
enum RunwayID { RWY_A, RWY_B, RWY_C };
enum Direction { NORTH, SOUTH, EAST, WEST };

// Structs
struct SpeedRule {
    double minSpeed;
    double maxSpeed;
    string violationCriteria;
};

struct Aircraft {
    string id;
    string airline;
    AircraftType type;
    FlightPhase phase;
    double currentSpeed;
    bool isEmergency;
    Direction direction;
    bool hasAVN;
    RunwayID assignedRunway;
    bool hasFault;
    int priority;
    string scheduledTimeStr;
    int scheduledMinutes;
    int mappedSimSecond;
    time_t lastPhaseChange;
    time_t queueEntryTime; // Time when added to queue
    double waitTime;       // Waiting time in seconds
    double fuelPercentage; // Fuel level (0-100%)
    bool hadLowFuel;       // Tracks if low fuel emergency occurred

    Aircraft() : assignedRunway(static_cast<RunwayID>(-1)), waitTime(0.0), fuelPercentage(100.0), hadLowFuel(false) {}

    // For display purposes
    string getPhaseString() const {
        switch(phase) {
            case HOLDING: return "Holding";
            case APPROACH: return "Approach";
            case LANDING: return "Landing";
            case TAXI: return "Taxiing";
            case AT_GATE: return "At Gate";
            case TAKEOFF_ROLL: return "Takeoff";
            case CLIMB: return "Climbing";
            case CRUISE: return "Cruising";
            default: return "Unknown";
        }
    }

    string getTypeString() const {
        switch(type) {
            case COMMERCIAL: return "Commercial";
            case CARGO: return "Cargo";
            case EMERGENCY: return "Emergency";
            default: return "Unknown";
        }
    }

    string getRunwayString() const {
        switch(assignedRunway) {
            case RWY_A: return "RWY-A";
            case RWY_B: return "RWY-B";
            case RWY_C: return "RWY-C";
            default: return "None";
        }
    }
};

struct Runway {
    RunwayID id;
    atomic<bool> isOccupied;
    mutex mtx;
    condition_variable cv;
    Aircraft* currentAircraft;

    string getName() const {
        switch(id) {
            case RWY_A: return "RWY-A (Arrivals)";
            case RWY_B: return "RWY-B (Departures)";
            case RWY_C: return "RWY-C (Cargo/Emergency)";
            default: return "Unknown";
        }
    }
};

// Comparator for priority queue
struct AircraftComparator {
    bool operator()(const Aircraft* a, const Aircraft* b) const {
        if (a->mappedSimSecond != b->mappedSimSecond) {
            return a->mappedSimSecond > b->mappedSimSecond; // Earlier time first
        }
        return a->priority < b->priority; // Higher priority first
    }
};

class AirControlX {
private:
    vector<Aircraft> flights;
    array<Runway, MAX_RUNWAYS> runways;
    atomic<int> simulationTime;
    mutex logMutex;
    mutex displayMutex;
    ofstream logFile;

    // Priority queues for each runway
    priority_queue<Aircraft*, vector<Aircraft*>, AircraftComparator> arrivalQueue;
    priority_queue<Aircraft*, vector<Aircraft*>, AircraftComparator> departureQueue;
    priority_queue<Aircraft*, vector<Aircraft*>, AircraftComparator> cargoEmergencyQueue;
    mutex arrivalQueueMutex;
    mutex departureQueueMutex;
    mutex cargoQueueMutex;

    // Thread management
    vector<thread> flightThreads;
    vector<thread> runwayThreads;
    atomic<bool> simulationRunning;

public:
    AirControlX() : simulationTime(0), simulationRunning(false) {
        // Initialize runways
        runways[RWY_A].id = RWY_A;
        runways[RWY_B].id = RWY_B;
        runways[RWY_C].id = RWY_C;
        runways[RWY_A].isOccupied = false;
        runways[RWY_B].isOccupied = false;
        runways[RWY_C].isOccupied = false;
        runways[RWY_A].currentAircraft = nullptr;
        runways[RWY_B].currentAircraft = nullptr;
        runways[RWY_C].currentAircraft = nullptr;

        logFile.open("log.txt", ios::out);
        if (!logFile.is_open()) {
            cerr << "Failed to open log file!" << endl;
            exit(1); // Exit if log file can't be opened
        }
    }

    ~AirControlX() {
        if (logFile.is_open()) {
            logFile.close();
        }
    }

    void logEvent(const string& message) {
        lock_guard<mutex> lock(logMutex);
        logFile << message << endl;
        cout << message << endl;
    }

    SpeedRule getSpeedRule(FlightPhase phase) {
        SpeedRule rule;
        switch (phase) {
            case HOLDING:
                rule.minSpeed = 400;
                rule.maxSpeed = 600;
                rule.violationCriteria = "Speed exceeds 600 km/h";
                break;
            case APPROACH:
                rule.minSpeed = 240;
                rule.maxSpeed = 290;
                rule.violationCriteria = "Speed below 240 or above 290 km/h";
                break;
            case LANDING:
                rule.minSpeed = 30;
                rule.maxSpeed = 240;
                rule.violationCriteria = "Speed exceeds 240 or fails to slow below 30 km/h";
                break;
            case TAXI:
                rule.minSpeed = 15;
                rule.maxSpeed = 30;
                rule.violationCriteria = "Speed exceeds 30 km/h";
                break;
            case AT_GATE:
                rule.minSpeed = 0;
                rule.maxSpeed = 5;
                rule.violationCriteria = "Speed exceeds 5 km/h";
                break;
            case TAKEOFF_ROLL:
                rule.minSpeed = 0;
                rule.maxSpeed = 290;
                rule.violationCriteria = "Speed exceeds 290 km/h";
                break;
            case CLIMB:
                rule.minSpeed = 250;
                rule.maxSpeed = 463;
                rule.violationCriteria = "Speed exceeds 463 km/h";
                break;
            case CRUISE:
                rule.minSpeed = 800;
                rule.maxSpeed = 900;
                rule.violationCriteria = "Speed below 800 or above 900 km/h";
                break;
            default:
                rule.minSpeed = 0;
                rule.maxSpeed = 0;
                rule.violationCriteria = "Unknown phase";
                break;
        }
        return rule;
    }

    void monitorSpeed(Aircraft& aircraft) {
        if (aircraft.phase == AT_GATE) return;

        SpeedRule rule = getSpeedRule(aircraft.phase);

        if (aircraft.currentSpeed < rule.minSpeed || aircraft.currentSpeed > rule.maxSpeed) {
            if (!aircraft.hasAVN) {
                aircraft.hasAVN = true;
                string msg = "[SPEED MONITOR] AVN Issued for " + aircraft.id + ": " +
                             rule.violationCriteria + " (" + to_string(aircraft.currentSpeed) + " km/h)";
                logEvent(msg);
            }
        }
    }

    void updateFlightPhase(Aircraft& aircraft) {
        time_t now = time(nullptr);

        if (aircraft.hasFault || aircraft.assignedRunway < 0 || aircraft.assignedRunway >= MAX_RUNWAYS) {
            return; // Invalid runway ID
        }

        // Arrivals (North/South)
        if ((aircraft.direction == NORTH || aircraft.direction == SOUTH) &&
            (aircraft.phase == HOLDING || aircraft.phase == APPROACH)) {
            if (aircraft.phase == HOLDING) {
                aircraft.currentSpeed = 400 + (rand() % 201); // 400-600 km/h
                if (difftime(now, aircraft.lastPhaseChange) > 5) {
                    aircraft.phase = APPROACH;
                    aircraft.lastPhaseChange = now;
                    logEvent("[PHASE] " + aircraft.id + " moved to APPROACH.");
                }
            } else if (aircraft.phase == APPROACH) {
                aircraft.currentSpeed = 240 + (rand() % 51); // 240-290 km/h
                if (difftime(now, aircraft.lastPhaseChange) > 5) {
                    aircraft.phase = LANDING;
                    aircraft.lastPhaseChange = now;
                    logEvent("[PHASE] " + aircraft.id + " moved to LANDING.");
                }
            }
            return;
        }

        // Everything below needs a runway
        Runway& runway = runways[aircraft.assignedRunway];
        unique_lock<mutex> lock(runway.mtx, try_to_lock);
        if (!lock.owns_lock()) {
            return;
        }

        // Arrivals
        if (aircraft.direction == NORTH || aircraft.direction == SOUTH) {
            switch (aircraft.phase) {
                case LANDING:
                    aircraft.currentSpeed -= 30 + (rand() % 50);
                    if (aircraft.currentSpeed <= 30) {
                        aircraft.phase = TAXI;
                        aircraft.currentSpeed = 15 + (rand() % 16);
                        aircraft.lastPhaseChange = now;
                        logEvent("[PHASE] " + aircraft.id + " moved to TAXI.");
                    }
                    break;
                case TAXI:
                    if (difftime(now, aircraft.lastPhaseChange) > 5) {
                        aircraft.phase = AT_GATE;
                        aircraft.currentSpeed = 0;
                        aircraft.lastPhaseChange = now;
                        logEvent("[PHASE] " + aircraft.id + " reached GATE.");
                        runway.isOccupied = false;
                        runway.currentAircraft = nullptr;
                        runway.cv.notify_one();
                    }
                    break;
                default:
                    break;
            }
        }
        // Departures
        else if (aircraft.direction == EAST || aircraft.direction == WEST) {
            switch (aircraft.phase) {
                case AT_GATE:
                    if (difftime(now, aircraft.lastPhaseChange) > 5) {
                        aircraft.phase = TAXI;
                        aircraft.currentSpeed = 15 + (rand() % 16);
                        aircraft.lastPhaseChange = now;
                        logEvent("[PHASE] " + aircraft.id + " moved to TAXI.");
                    }
                    break;
                case TAXI:
                    if (difftime(now, aircraft.lastPhaseChange) > 5) {
                        aircraft.phase = TAKEOFF_ROLL;
                        aircraft.currentSpeed = 0;
                        aircraft.lastPhaseChange = now;
                        logEvent("[PHASE] " + aircraft.id + " moved to TAKEOFF.");
                    }
                    break;
                case TAKEOFF_ROLL:
                    aircraft.currentSpeed += 30 + (rand() % 200);
                    if (aircraft.currentSpeed >= 250) {
                        aircraft.phase = CLIMB;
                        aircraft.currentSpeed = 250 + (rand() % 214);
                        aircraft.lastPhaseChange = now;
                        logEvent("[PHASE] " + aircraft.id + " moved to CLIMB.");
                        runway.isOccupied = false;
                        runway.currentAircraft = nullptr;
                        runway.cv.notify_one();
                    }
                    break;
                case CLIMB:
                    if (difftime(now, aircraft.lastPhaseChange) > 5) {
                        aircraft.phase = CRUISE;
                        aircraft.currentSpeed = 800 + (rand() % 101);
                        aircraft.lastPhaseChange = now;
                        logEvent("[PHASE] " + aircraft.id + " reached CRUISE.");
                    }
                    break;
                default:
                    break;
            }
        }
    }

    void radarMonitor(Aircraft& aircraft) {
        while (simulationRunning) {
            {
                lock_guard<mutex> lock(displayMutex);
                monitorSpeed(aircraft);
                
                
                                     // Fuel consumption for arriving flights in DEPARTURE : TAKEOFF, CLIMBING, CRUISING
                     // we don't need this but aiwein, just for looks 
                if ( !aircraft.hasFault  &&( aircraft.direction == EAST || aircraft.direction == WEST) &&
                    (aircraft.phase == TAKEOFF_ROLL || aircraft.phase == CLIMB || aircraft.phase == CRUISE)) {
                    	if(aircraft.fuelPercentage<=LOW_FUEL_THRESHOLD) {} // do nothing if it goes below the threshold
                    	else 
                    		aircraft.fuelPercentage -= rand() % 3; // chhota sa number just for the simulation 
                    }

                // Fuel consumption for arriving flights in ARRIVAL : HOLDING, APPROACH, or LANDING
                if ((aircraft.direction == NORTH || aircraft.direction == SOUTH) &&
                    (aircraft.phase == HOLDING || aircraft.phase == APPROACH || aircraft.phase == LANDING)) {
                    	if(aircraft.fuelPercentage<=LOW_FUEL_THRESHOLD)            // warna fuel khatam ho jaata hai and it still hasn't landed so just decrease thora thora
                    		aircraft.fuelPercentage -= rand() % 3;
                    	else
                    		aircraft.fuelPercentage -= rand() % 10;
                    if (aircraft.fuelPercentage < 0) aircraft.fuelPercentage = 0;
                    

                    // Check for low fuel emergency
                    if (aircraft.fuelPercentage < LOW_FUEL_THRESHOLD && !aircraft.isEmergency) {
                        aircraft.isEmergency = true;
                        aircraft.type = EMERGENCY;
                        aircraft.priority = 5;
                        aircraft.hadLowFuel = true;

                        // Move to cargoEmergencyQueue if not already there
                        bool moved = false;
                        if (aircraft.direction == NORTH || aircraft.direction == SOUTH) {
                            lock_guard<mutex> lock(arrivalQueueMutex);
                            priority_queue<Aircraft*, vector<Aircraft*>, AircraftComparator> temp;
                            while (!arrivalQueue.empty()) {
                                Aircraft* a = arrivalQueue.top();
                                arrivalQueue.pop();
                                if (a != &aircraft) temp.push(a);
                                else moved = true;
                            }
                            arrivalQueue = temp;
                        } else if (aircraft.direction == EAST || aircraft.direction == WEST) {
                            lock_guard<mutex> lock(departureQueueMutex);
                            priority_queue<Aircraft*, vector<Aircraft*>, AircraftComparator> temp;
                            while (!departureQueue.empty()) {
                                Aircraft* a = departureQueue.top();
                                departureQueue.pop();
                                if (a != &aircraft) temp.push(a);
                                else moved = true;
                            }
                            departureQueue = temp;
                        }
                        if (moved) {
                            lock_guard<mutex> lock(cargoQueueMutex);
                            cargoEmergencyQueue.push(&aircraft);
                        }

                        string msg = "[FUEL] Low fuel emergency for " + aircraft.id +
                                     ". Set as EMERGENCY, moved to RWY-C queue.";
                        logEvent(msg);
                    }
                }

                // Fault check
                if (((aircraft.direction==EAST||aircraft.direction == WEST) &&( aircraft.phase == AT_GATE || aircraft.phase == TAXI)) && !aircraft.hasFault) {
                    int chance = 1+ rand() % 100;
                    int faultProb = 0;
                    switch (aircraft.direction) {
                        case NORTH: faultProb = 10; break;
                        case SOUTH: faultProb = 5; break;
                        case EAST: faultProb = 15; break;
                        case WEST: faultProb = 20; break;
                    }

                    if (chance < faultProb) {
                        aircraft.hasFault = true;
                        aircraft.phase = AT_GATE;
                        aircraft.currentSpeed = 0;
                        aircraft.lastPhaseChange = time(nullptr);
                        string msg = "[FAULT] Ground fault detected in " + aircraft.id + ". Aircraft towed to GATE.";
                        //aircraft.isFlight = false;
                        logEvent(msg);
                        

                        // Remove from queue
                        if (aircraft.type == CARGO || aircraft.type == EMERGENCY) {
                            lock_guard<mutex> lock(cargoQueueMutex);
                            priority_queue<Aircraft*, vector<Aircraft*>, AircraftComparator> temp;
                            while (!cargoEmergencyQueue.empty()) {
                                Aircraft* a = cargoEmergencyQueue.top();
                                cargoEmergencyQueue.pop();
                                if (a != &aircraft) temp.push(a);
                            }
                            cargoEmergencyQueue = temp;
                        } else if (aircraft.direction == NORTH || aircraft.direction == SOUTH) {
                            lock_guard<mutex> lock(arrivalQueueMutex);
                            priority_queue<Aircraft*, vector<Aircraft*>, AircraftComparator> temp;
                            while (!arrivalQueue.empty()) {
                                Aircraft* a = arrivalQueue.top();
                                arrivalQueue.pop();
                                if (a != &aircraft) temp.push(a);
                            }
                            arrivalQueue = temp;
                        } else {
                            lock_guard<mutex> lock(departureQueueMutex);
                            priority_queue<Aircraft*, vector<Aircraft*>, AircraftComparator> temp;
                            while (!departureQueue.empty()) {
                                Aircraft* a = departureQueue.top();
                                departureQueue.pop();
                                if (a != &aircraft) temp.push(a);
                            }
                            departureQueue = temp;
                        }
                    }
                }
            }
            this_thread::sleep_for(chrono::seconds(1));
        }
    }

    void runwayController(Runway& runway) {
        priority_queue<Aircraft*, vector<Aircraft*>, AircraftComparator>* assignedQueue = nullptr;
        mutex* queueMutex = nullptr;

        // Determine queue
        if (runway.id == RWY_A) {
            assignedQueue = &arrivalQueue;
            queueMutex = &arrivalQueueMutex;
        } else if (runway.id == RWY_B) {
            assignedQueue = &departureQueue;
            queueMutex = &departureQueueMutex;
        } else {
            assignedQueue = &cargoEmergencyQueue;
            queueMutex = &cargoQueueMutex;
        }

        while (simulationRunning) {
            Aircraft* nextAircraft = nullptr;

            // Check for aircraft in queue
            {
                lock_guard<mutex> lock(*queueMutex);
                if (!assignedQueue->empty()) {
                    nextAircraft = assignedQueue->top();
                    if (nextAircraft->mappedSimSecond <= simulationTime) {
                        assignedQueue->pop();
                    } else {
                        nextAircraft = nullptr; // Not yet scheduled
                    }
                }
            }

            // Proceed if flight is ready and runway is free
            if (nextAircraft && !nextAircraft->hasFault) {
                // Wait for runway to be available
                unique_lock<mutex> runwayLock(runway.mtx);
                runway.cv.wait(runwayLock, [&](){ return !runway.isOccupied; });

                // Calculate waiting time
                time_t now = time(nullptr);
                nextAircraft->waitTime = difftime(now, nextAircraft->queueEntryTime);

                // Assign aircraft to runway
                runway.isOccupied = true;
                runway.currentAircraft = nextAircraft;
                nextAircraft->assignedRunway = runway.id;

                string msg = "[RUNWAY] " + nextAircraft->id + " assigned to " + runway.getName() +
                             " (Waited: " + to_string(nextAircraft->waitTime) + "s, Fuel: " +
                             to_string(nextAircraft->fuelPercentage) + "%)";
                logEvent(msg);

                // Simulate runway operation
                this_thread::sleep_for(chrono::seconds(5));

                // Release runway
                runway.isOccupied = false;
                runway.currentAircraft = nullptr;
                runway.cv.notify_one();

                msg = "[RUNWAY] " + nextAircraft->id + " completed operation on " + runway.getName();
                logEvent(msg);
            }

            this_thread::sleep_for(chrono::milliseconds(100)); // Prevent busy waiting
        }
    }

    void displayStatus() {
        while (simulationRunning) {
            system("clear"); // Clear the screen

            lock_guard<mutex> lock(displayMutex);

            cout << "=== AirControlX Status ===" << endl;
            cout << "Simulation Time: " << simulationTime << "/" << SIMULATION_DURATION << " seconds" << endl << endl;

            // Display runways
            cout << "=== Runways ===" << endl;
            for (auto& runway : runways) {
                cout << runway.getName() << ": ";
                if (runway.isOccupied && runway.currentAircraft) {
                    cout << runway.currentAircraft->id << " (" << runway.currentAircraft->getPhaseString() << ")";
                } else {
                    cout << "Available";
                }
                cout << endl;
            }
            cout << endl;

            // Display flights
            cout << "=== Active Flights ===" << endl;
            cout << left << setw(10) << "Flight ID" << setw(12) << "Type"
                 << setw(10) << "Phase" << setw(10) << "Speed"
                 << setw(10) << "Priority" << setw(10) << "Runway"
                 << setw(5) << "AVN" << setw(8) << "Wait(s)"
                 << setw(10) << "Fuel(%)" << endl;
            cout << string(80, '-') << endl;
  
            for (auto& flight : flights) //format the output
            {
                cout << left << setw(10) << flight.id
                     << setw(12) << flight.getTypeString()             
                     << setw(12) << (flight.hasFault ? "TOWED" : flight.getPhaseString()) //show towed in output
                     << setw(10) << fixed << setprecision(2) << flight.currentSpeed
                     << setw(10) << flight.priority
                     << setw(10) << flight.getRunwayString()
                     << setw(5) << (flight.hasAVN ? "Yes" : "No")
                     << setw(8) << fixed << setprecision(2) << flight.waitTime
                     << setw(10) << fixed << setprecision(2) << flight.fuelPercentage << endl;
            }

            this_thread::sleep_for(chrono::seconds(1));
        }
    }

    void inputFlights() {
        int n;
        do {
            cout << "Enter number of flights to schedule (Max " << MAX_ACTIVE_FLIGHTS << "): ";
            cin >> n;
            if ( cin.fail()){
                    cin.clear();
            	n=-1;
	}
            cin.ignore();
            if (n <= 0 || n > MAX_ACTIVE_FLIGHTS) {
                cout << "Invalid. Try again." << endl;
            }
        } while (n <= 0 || n > MAX_ACTIVE_FLIGHTS);

        for (int i = 0; i < n; i++) {
            Aircraft ac;
            cout << "\nFlight " << i+1 << ":" << endl;

            cout << "Flight ID: ";
            getline(cin, ac.id);
            if (ac.id.empty()) {
                cout << "Flight ID cannot be empty. Try again." << endl;
                i--;
                continue;
            }

            cout << "Airline Name: ";
            getline(cin, ac.airline);

            int typeInput;
            do {
                cout << "Aircraft Type:\n0: Commercial\n1: Cargo\n2: Emergency [Medical/Military/Ambulance]" << endl;
                cin >> typeInput;
                   if ( cin.fail()){
		            cin.clear();
		    	typeInput=-1;
		}
                cin.ignore();
                if (typeInput < 0 || typeInput > 2)
                    cout << "Invalid. Try again." << endl;
            } while (typeInput < 0 || typeInput > 2);
            ac.type = static_cast<AircraftType>(typeInput);

            int dirInput;
            do {
                cout << "Direction:\n0: North\n1: South\n2: East\n3: West" << endl;
                cin >> dirInput;
                   if ( cin.fail()){
		            cin.clear();
		            dirInput=-1;
		}
                cin.ignore();
                if (dirInput < 0 || dirInput > 3)
                    cout << "Invalid. Try again." << endl;
            } while (dirInput < 0 || dirInput > 3);
            ac.direction = static_cast<Direction>(dirInput);

            // Set priority
            int priorityInput;
            do {
                cout << "Priority (1-5, 5=high): ";
                cin >> priorityInput;
                   if ( cin.fail()){
		        cin.clear();
		    	priorityInput=-1;
		}
                cin.ignore();
                if (priorityInput < 1 || priorityInput > 5)
                    cout << "Invalid. Try again." << endl;

            } while (priorityInput < 1 || priorityInput > 5);
            ac.priority = priorityInput;

            // Validate time
            bool validTime = false;
            while (!validTime) {
                cout << "Scheduled Time (hh:mm): ";
                getline(cin, ac.scheduledTimeStr);

                int hh, mm;
                if (sscanf(ac.scheduledTimeStr.c_str(), "%d:%d", &hh, &mm) == 2) {
                    if (hh >= 0 && hh < 24 && mm >= 0 && mm < 60) {
                        ac.scheduledMinutes = hh * 60 + mm;
                        validTime = true;
                    } else {
                        cout << "Invalid time. Hours (0-23), Minutes (0-59)." << endl;
                    }
                } else {
                    cout << "Invalid format. Please enter a value like 13:45." << endl;
                }
            }

            ac.phase = (ac.direction == NORTH || ac.direction == SOUTH) ? HOLDING : AT_GATE;
            ac.currentSpeed = (ac.phase == HOLDING) ? 400 + (rand() % 201) : 0;
            ac.isEmergency = (ac.type == EMERGENCY);
            ac.hasAVN = false;
            ac.hasFault = false;
            ac.lastPhaseChange = time(nullptr);
            ac.queueEntryTime = time(nullptr);
            ac.fuelPercentage = (ac.direction == NORTH || ac.direction == SOUTH) ? (70 + (rand() % 31)) : 100.0;  // b/w 70-100 for arrivals, 100 for departures
            ac.hadLowFuel = false;

            flights.push_back(ac);
        }
    }

    void mapScheduledTimes() {
        if (flights.empty()) return;

        int minMinutes = flights[0].scheduledMinutes;
        int maxMinutes = flights[0].scheduledMinutes;

        for (auto& flight : flights) {
            if (flight.scheduledMinutes < minMinutes) minMinutes = flight.scheduledMinutes;
            if (flight.scheduledMinutes > maxMinutes) maxMinutes = flight.scheduledMinutes;
        }

        int realTimeWindow = maxMinutes - minMinutes;
        if (realTimeWindow == 0) realTimeWindow = 1;

        for (auto& flight : flights) {
            int relativeMinute = flight.scheduledMinutes - minMinutes;
            double ratio = static_cast<double>(relativeMinute) / realTimeWindow;
            flight.mappedSimSecond = static_cast<int>(ratio * SIMULATION_DURATION);
        }
    }

    void scheduleFlights() {
        // Sort flights by scheduled time and priority
        sort(flights.begin(), flights.end(), [](const Aircraft& a, const Aircraft& b) {
            if (a.mappedSimSecond != b.mappedSimSecond) {
                return a.mappedSimSecond < b.mappedSimSecond;
            }
            return a.priority > b.priority;
        });

        // Assign to queues
        for (auto& flight : flights) {
            flight.queueEntryTime = time(nullptr); // Record queue entry time
            if (flight.type == CARGO || flight.type == EMERGENCY || flight.isEmergency) {
                lock_guard<mutex> lock(cargoQueueMutex);
                cargoEmergencyQueue.push(&flight);
            } else if (flight.direction == NORTH || flight.direction == SOUTH) {
                lock_guard<mutex> lock(arrivalQueueMutex);
                arrivalQueue.push(&flight);
            } else {
                lock_guard<mutex> lock(departureQueueMutex);
                departureQueue.push(&flight);
            }
        }
    }

    void summarizeSimulation() {
        lock_guard<mutex> lock(displayMutex);
        cout << "\n=== Simulation Summary ===" << endl;
        cout << "Total Flights: " << flights.size() << endl;

        int totalAVNs = 0, totalFaults = 0, totalLowFuel = 0;
        double totalWaitTime = 0.0;
        int processedFlights = 0;

        for (const auto& flight : flights) {
            if (flight.hasAVN) totalAVNs++;
            if (flight.hasFault) totalFaults++;
            if (flight.hadLowFuel) totalLowFuel++;
            if (flight.waitTime > 0.0) {
                totalWaitTime += flight.waitTime;
                processedFlights++;
            }
        }

        double avgWaitTime = processedFlights > 0 ? totalWaitTime / processedFlights : 0.0;
        cout << "Total AVNs Issued: " << totalAVNs << endl;
        cout << "Total Faults Detected: " << totalFaults << endl;
        cout << "Total Low Fuel Emergencies: " << totalLowFuel << endl;
        cout << "Average Waiting Time: " << fixed << setprecision(2) << avgWaitTime << " seconds" << endl;
        cout << "==========================" << endl;

        string msg = "[SUMMARY] Flights: " + to_string(flights.size()) +
                     ", AVNs: " + to_string(totalAVNs) +
                     ", Faults: " + to_string(totalFaults) +
                     ", Low Fuel: " + to_string(totalLowFuel) +
                     ", Avg Wait: " + to_string(avgWaitTime) + "s";
        logEvent(msg);
    }

    void startSimulation() {
        simulationRunning = true;
        simulationTime = 0;

        // Start runway controller threads
        for (auto& runway : runways) {
            runwayThreads.emplace_back(&AirControlX::runwayController, this, ref(runway));
        }

        // Start radar monitoring threads for each flight
        for (auto& flight : flights) {
            flightThreads.emplace_back(&AirControlX::radarMonitor, this, ref(flight));
        }

        // Start display thread
        thread displayThread(&AirControlX::displayStatus, this);

        // Simulation timer
        while (simulationTime < SIMULATION_DURATION) {
            this_thread::sleep_for(chrono::seconds(1));
            simulationTime++;

            // Update flight phases
            for (auto& flight : flights) {
                updateFlightPhase(flight);
            }
            
          //exit early if all aircraft are either cruising or towed
          bool allDone = all_of(flights.begin(), flights.end(), [](const Aircraft& ac)
          { return ac.phase == CRUISE |ac.hasFault | (ac.phase==AT_GATE&&!ac.hasFault); }
          );
          if (allDone) 
              break; //exit early
            
        }

        // Clean up
        simulationRunning = false;

        for (auto& thread : runwayThreads) {
            if (thread.joinable()) thread.join();
        }

        for (auto& thread : flightThreads) {
            if (thread.joinable()) thread.join();
        }

        if (displayThread.joinable()) displayThread.join();

        summarizeSimulation();
        cout << "\nSimulation Complete!" << endl;
    }
};

int main() {
    srand(time(nullptr));

    AirControlX atc;

    atc.inputFlights();
    atc.mapScheduledTimes();
    atc.scheduleFlights();
    atc.startSimulation();

    return 0;
}
