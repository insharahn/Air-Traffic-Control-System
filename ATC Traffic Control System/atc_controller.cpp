//AABIA ALI 23I-0704
//INSHARAH IRFAN 23I-0615
//CS-D

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
#include <unistd.h>
#include <SFML/Graphics.hpp>


using namespace std;

// Constants
const int MAX_ACTIVE_FLIGHTS = 20;
const int MAX_RUNWAYS = 3;
const int SIMULATION_DURATION = 300; // 5 minutes in seconds
const double LOW_FUEL_THRESHOLD = 20.0; // 20%

// for sfml window - resize to change window size
const int resolutionX = 800;
const int resolutionY = 600;

//for input
struct FlightInputData 
{
    string numFlights;
    string id;
    string airline;
    string type;
    string direction;
    string priority;
    string scheduledTime;
    int currentField = 0;
    bool complete = false;
};
int totalFlightsToEnter = 0;
int currentFlightNumber = 0;
vector<FlightInputData> allFlightInputs;

//for child process
int TotalAVNs = 0;


enum AppState { INPUT_STATE, SIMULATION_STATE }; //switch between input and simulation
AppState currentState = INPUT_STATE; //always start on input

// Enums for acutal air traffic
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
    double waitTime; // Waiting time in seconds
    double fuelPercentage; // Fuel level (0-100%)
    bool hadLowFuel; // Tracks if low fuel emergency occurred
    int AVNcount = 0; //new: track the count of avns issued 


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
public: //delcaring these publically so that the stupid sfml windows can access this data
    vector<Aircraft> flights;
    array<Runway, MAX_RUNWAYS> runways;
    atomic<int> simulationTime;
    mutable mutex logMutex; //new mutable so that sfml walay functions can access it
    mutable mutex displayMutex; //new for display
    ofstream logFile;

    vector<string> consoleOutput; //new for console output
    atomic<bool> simulationComplete{false}; //new for ending screen when program ends

    // Priority queues for each runway
    priority_queue<Aircraft*, vector<Aircraft*>, AircraftComparator> arrivalQueue;
    priority_queue<Aircraft*, vector<Aircraft*>, AircraftComparator> departureQueue;
    priority_queue<Aircraft*, vector<Aircraft*>, AircraftComparator> cargoEmergencyQueue;
    mutable mutex arrivalQueueMutex;
    mutable mutex departureQueueMutex;
    mutable mutex cargoQueueMutex;

    // Thread management
    vector<thread> flightThreads;
    vector<thread> runwayThreads;
    atomic<bool> simulationRunning;

    //for child process
    int pipe_fd[2]; // Pipe for AVN Generator communication


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
        consoleOutput.push_back(message); //new: we need the message for the console as well
    }

     //new: getter for console output
    const vector<string>& getConsoleOutput() const 
    {
        return consoleOutput;
    }

    //get the most recent message
    string getLatestMessage() const
    {
        lock_guard<mutex> lock(logMutex);
        return consoleOutput.empty() ? "" : consoleOutput.back();
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

        if (aircraft.currentSpeed < rule.minSpeed || aircraft.currentSpeed > rule.maxSpeed) 
        {
            if (!aircraft.hasAVN) 
            {
                aircraft.hasAVN = true;
                aircraft.AVNcount++; //increment avn count
                TotalAVNs++;

                string msg = "[SPEED MONITOR] AVN Issued for " + aircraft.id + ": " +
                             rule.violationCriteria + " (" + to_string(aircraft.currentSpeed) + " km/h)";
                logEvent(msg);

                //formulate space separated string for AVN generator
                string AVNissued = aircraft.id + " " + aircraft.airline + " " + to_string(aircraft.type)  + " " + to_string(aircraft.currentSpeed) + " " + to_string(aircraft.phase) + " " + to_string(rule.minSpeed) + " " +to_string(rule.maxSpeed) + "\n";
                // write AVN details to pipe
                write(this->pipe_fd[1], AVNissued.c_str(), AVNissued.size());
            }

            //reset avn flag
            if (aircraft.currentSpeed > rule.minSpeed && aircraft.currentSpeed < rule.maxSpeed && aircraft.hasAVN) 
            {
                aircraft.hasAVN = false;
            }
            
        }
    }

    void updateFlightPhase(Aircraft& aircraft) {
        time_t now = time(nullptr);

        if (aircraft.hasFault || aircraft.assignedRunway < 0 || aircraft.assignedRunway >= MAX_RUNWAYS) {
            return; // Invalid runway ID
        }

        // Arrivals (North/South)
        if ((aircraft.direction == NORTH || aircraft.direction == SOUTH || aircraft.isEmergency) &&
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
                if (difftime(now, aircraft.lastPhaseChange) > 20) {
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

             //new : check emergency and runway first
             //fallback phase for an aircraft on runway c that had a random direction so random phase
             if (aircraft.isEmergency || aircraft.assignedRunway == RWY_C) 
             {
                switch (aircraft.phase) {
                    case HOLDING:
                        aircraft.currentSpeed = 400 + (rand() % 201);
                        if (difftime(now, aircraft.lastPhaseChange) > 20) {
                            aircraft.phase = APPROACH;
                            aircraft.lastPhaseChange = now;
                            logEvent("[PHASE] " + aircraft.id + " (RWY-C) moved to APPROACH.");
                        }
                        break;
                    case APPROACH:
                        aircraft.currentSpeed = 240 + (rand() % 51);
                        if (difftime(now, aircraft.lastPhaseChange) > 20) {
                            aircraft.phase = LANDING;
                            aircraft.lastPhaseChange = now;
                            logEvent("[PHASE] " + aircraft.id + " (RWY-C) moved to LANDING.");
                        }
                        break;
                    case LANDING:
                        aircraft.currentSpeed -= 30 + (rand() % 50);
                        if (aircraft.currentSpeed <= 30) {
                            aircraft.phase = TAXI;
                            aircraft.currentSpeed = 15 + (rand() % 16);
                            aircraft.lastPhaseChange = now;
                            logEvent("[PHASE] " + aircraft.id + " (RWY-C) moved to TAXI.");
                        }
                        break;
                    case TAXI:
                        if (difftime(now, aircraft.lastPhaseChange) > 20) {
                            aircraft.phase = AT_GATE;
                            aircraft.currentSpeed = 0;
                            aircraft.lastPhaseChange = now;
                            logEvent("[PHASE] " + aircraft.id + " (RWY-C) reached GATE.");
                            runway.isOccupied = false;
                            runway.currentAircraft = nullptr;
                            runway.cv.notify_one();
                        }
                        break;
                    case AT_GATE:
                        if ((aircraft.direction == EAST || aircraft.direction == WEST) &&
                            difftime(now, aircraft.lastPhaseChange) > 20) {
                            aircraft.phase = TAXI;
                            aircraft.currentSpeed = 15 + (rand() % 16);
                            aircraft.lastPhaseChange = now;
                            logEvent("[PHASE] " + aircraft.id + " (RWY-C) departed GATE to TAXI.");
                        }
                        break;
                    case TAKEOFF_ROLL:
                        aircraft.currentSpeed += 30 + (rand() % 200);
                        if (aircraft.currentSpeed >= 250) {
                            aircraft.phase = CLIMB;
                            aircraft.currentSpeed = 250 + (rand() % 214);
                            aircraft.lastPhaseChange = now;
                            logEvent("[PHASE] " + aircraft.id + " (RWY-C) moved to CLIMB.");
                            runway.isOccupied = false;
                            runway.currentAircraft = nullptr;
                            runway.cv.notify_one();
                        }
                        break;
                    case CLIMB:
                        if (difftime(now, aircraft.lastPhaseChange) > 20) {
                            aircraft.phase = CRUISE;
                            aircraft.currentSpeed = 800 + (rand() % 101);
                            aircraft.lastPhaseChange = now;
                            logEvent("[PHASE] " + aircraft.id + " (RWY-C) reached CRUISE.");
                        }
                        break;
                    default:
                        break;
                }
            }   

        // Arrivals
        else if (aircraft.direction == NORTH || aircraft.direction == SOUTH) {
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
                    if (difftime(now, aircraft.lastPhaseChange) > 20) {
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
                    if (difftime(now, aircraft.lastPhaseChange) > 20) {
                        aircraft.phase = TAXI;
                        aircraft.currentSpeed = 15 + (rand() % 16);
                        aircraft.lastPhaseChange = now;
                        logEvent("[PHASE] " + aircraft.id + " moved to TAXI.");
                    }
                    break;
                case TAXI:
                    if (difftime(now, aircraft.lastPhaseChange) > 20) {
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
                    if (difftime(now, aircraft.lastPhaseChange) > 20) {
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
                    	if(aircraft.fuelPercentage<=LOW_FUEL_THRESHOLD) // do nothing if it goes below the threshold
                        {

                        } 
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
                    if (aircraft.fuelPercentage < 2) aircraft.fuelPercentage = 1; //keep it at 1
                    

                    // Check for low fuel emergency
                    if (aircraft.fuelPercentage < LOW_FUEL_THRESHOLD && !aircraft.isEmergency) {
                        aircraft.isEmergency = true;
                        aircraft.type = EMERGENCY;
                        aircraft.priority = 5;
                        aircraft.hadLowFuel = true;
                        aircraft.mappedSimSecond = simulationTime; //new: prioritize immediately by setting time to now

                        //new: if emergency detected an lock not acquired i.e. not already on runway, then move
                        // Check if the aircraft is already on a runway
                        bool isOnRunway = false; //check if this flight is already on the runway
                        string currentRunway = aircraft.getRunwayString();
                        if (aircraft.assignedRunway != static_cast<RunwayID>(-1)) 
                        {
                            Runway& runway = runways[aircraft.assignedRunway];
                            unique_lock<mutex> runwayLock(runway.mtx, try_to_lock);
                            if (runwayLock.owns_lock() && runway.currentAircraft == &aircraft &&
                                (aircraft.phase == LANDING || aircraft.phase == TAKEOFF_ROLL || aircraft.phase == TAXI)) {
                                isOnRunway = true;
                            }
                        }

                        // Move to cargoEmergencyQueue if not already there
                        if (!isOnRunway) //new: move if lock not acquired
                        {
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
                        
                            if (moved) { //changed runways
                                lock_guard<mutex> lock(cargoQueueMutex);
                                cargoEmergencyQueue.push(&aircraft);
                                aircraft.assignedRunway = RWY_C; //new: update runway
                            }
                            else //did not change runway despite being low fuel bc it was already on its own runwau
                            {
                                //log that the aircraft remains on its current runway
                                string msg = "[FUEL] Low fuel emergency for " + aircraft.id +
                                             ". Set as EMERGENCY, remains on " + currentRunway + ".";
                                logEvent(msg);
                            }

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
                if (!assignedQueue->empty()) 
                {
                    nextAircraft = assignedQueue->top();
                    //new check time OR emergency to prioritize
                    if (runway.id == RWY_C || nextAircraft->mappedSimSecond <= simulationTime) 
                    {
                        assignedQueue->pop();
                    } 
                    else 
                    {
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

               /*  // Release runway
                runway.isOccupied = false;
                runway.currentAircraft = nullptr; */

                 //new: release runway if aircraft is in LANDING, TAXI, or beyond
                 if (nextAircraft->phase == LANDING || nextAircraft->phase == TAXI ||
                    nextAircraft->phase == AT_GATE || nextAircraft->phase == CLIMB ||
                    nextAircraft->phase == CRUISE) {
                    runway.isOccupied = false;
                    runway.currentAircraft = nullptr;
                }

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
                     << setw(5) << flight.AVNcount //old: (flight.hasAVN ? "Yes" : "No")
                     << setw(8) << fixed << setprecision(2) << flight.waitTime
                     << setw(10) << fixed << setprecision(2) << flight.fuelPercentage << endl;
            }

            this_thread::sleep_for(chrono::seconds(1));
        }
    }

    void inputFlights() 
    {
        int n;
        do 
        {
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
    
    /*
    void mapScheduledTimes() {
    if (flights.empty()) return;

    // Find min and max scheduled minutes
    int minMinutes = flights[0].scheduledMinutes;
    int maxMinutes = flights[0].scheduledMinutes;

    for (auto& flight : flights) {
        if (flight.scheduledMinutes < minMinutes) minMinutes = flight.scheduledMinutes;
        if (flight.scheduledMinutes > maxMinutes) maxMinutes = flight.scheduledMinutes;
    }

    // Define the simulation window with offsets
    const int startOffset = 30; // Start at 30 seconds
    const int endBuffer = 60;  // End 60 seconds before simulation duration
    const int effectiveSimDuration = SIMULATION_DURATION - startOffset - endBuffer; // 300 - 30 - 60 = 210 seconds

    // Calculate the real-time window (in minutes)
    int realTimeWindow = maxMinutes - minMinutes;

    if (realTimeWindow == 0) {
        // If all flights have the same scheduled time, spread them evenly within the effective window
        for (size_t i = 0; i < flights.size(); ++i) {
            flights[i].mappedSimSecond = startOffset + (i * effectiveSimDuration) / flights.size();
        }
    } else {
        // Map proportionally to the effective simulation window
        for (auto& flight : flights) {
            double relativeMinute = static_cast<double>(flight.scheduledMinutes - minMinutes);
            double ratio = relativeMinute / realTimeWindow;
            // Map to the effective window [startOffset, SIMULATION_DURATION - endBuffer]
            flight.mappedSimSecond = startOffset + static_cast<int>(ratio * effectiveSimDuration);
        }
    }
  }*/
    


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
        cout << "Total AVNs Issued: " << TotalAVNs << endl;
        cout << "Total Faults Detected: " << totalFaults << endl;
        cout << "Total Low Fuel Emergencies: " << totalLowFuel << endl;
        cout << "Average Waiting Time: " << fixed << setprecision(2) << avgWaitTime << " seconds" << endl;
        cout << "==========================" << endl;

        string msg = "[SUMMARY] Flights: " + to_string(flights.size()) +
                     ", AVNs: " + to_string(TotalAVNs) +
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
            
          //exit early if all aircraft are either cruising or towed or at gate (depts)
          bool allDone = all_of(flights.begin(), flights.end(), [](const Aircraft& ac)
          { return ac.phase == CRUISE |ac.hasFault | (ac.phase==AT_GATE&&!ac.hasFault && (ac.direction == NORTH || ac.direction == SOUTH)); } //new end condition
          );
          if (allDone) 
          {
              simulationComplete = true;
              break; //exit early

          }
            
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

//structures and variables to help with input :( -----------------------------------------------------
class InputHandler
{
private:
    sf::Font font;
    vector<sf::Text> prompts;
    vector<sf::RectangleShape> inputBoxes;
    vector<sf::Text> inputTexts;
    sf::RectangleShape submitButton;
    sf::Text submitText;
    sf::Text errorText;
    
public:
    FlightInputData data;

    InputHandler() 
    {
        //load font
        if (!this->font.loadFromFile("Textures/VT323-Regular.ttf"))
        {
            cerr << "Error: Failed to font.\n";
            return;
        }

        //init error msg
        errorText.setFont(font);
        errorText.setCharacterSize(18);
        errorText.setFillColor(sf::Color::Red);
        errorText.setPosition(50, 470);

        //setup input fields
        vector<string> labels = {
            "Number of flights (1-20):",
            "Flight ID:",
            "Airline:",
            "Aircraft Type\n0: Commercial\n1: Cargo\n2: Emergency [Medical/Military/Ambulance]",
            "\nDirection\n0: North, 1: South\n2: East, 3: West",
            "\nPriority (1-5, 5 = High):",
            "Scheduled Time (hh:mm):"
        };

        for (size_t i = 0; i < labels.size(); i++) 
        {
            //prompt text
            sf::Text prompt(labels[i], font, 18); //sent font size and positons here
            prompt.setPosition(50, 55 + i*60);
            prompts.push_back(prompt);

            //input box
            sf::RectangleShape box(sf::Vector2f(300, 30));
            box.setPosition(400, 55 + i*60);
            box.setFillColor(sf::Color::White);
            box.setOutlineThickness(2);
            inputBoxes.push_back(box);

            //input text
            sf::Text text("", font, 30);
            text.setPosition(405, 50 + i*60);
            text.setFillColor(sf::Color::Black);
            inputTexts.push_back(text);
        }

        //submit button
        submitButton.setSize(sf::Vector2f(200, 40));
        submitButton.setPosition(300, 500);
        submitButton.setFillColor(sf::Color::Green);
        
        submitText.setString("Submit Flight");
        submitText.setFont(font);
        submitText.setCharacterSize(30);
        submitText.setPosition(320, 500);
    }

    void handleEvent(sf::Event& event) //keyboard, mouse events handler
    {
        if (event.type == sf::Event::TextEntered) 
        {
            if (event.text.unicode == '\b') //backspace
            { 
                if (!getCurrentInput().empty()) 
                {
                    getCurrentInput().pop_back();
                }
            }
            else if (event.text.unicode < 128) 
            {
                getCurrentInput() += static_cast<char>(event.text.unicode);
            }
        }
        else if (event.type == sf::Event::MouseButtonPressed) 
        {
            if (submitButton.getGlobalBounds().contains(
                event.mouseButton.x, event.mouseButton.y)) 
            {
                bool allValid = true;
                int prevField = data.currentField;

                 //skip numFlights field after first flight
                 int startField = (currentFlightNumber == 0) ? 0 : 1;
            
                for (int i = 0; i < 7; ++i) //total 7 fields
                {
                    data.currentField = i;
                    if (!validateInput()) //validate input for all fields
                    {
                        allValid = false;
                        break; //show error for first invalid field
                    }
                }
            
                data.currentField = prevField; //restore previous focus
            
                if (allValid) 
                {
                    if (currentFlightNumber == 0) //store the first flight's numFlights
                        totalFlightsToEnter = stoi(data.numFlights);
                    
                    allFlightInputs.push_back(data); //store data in vector
                    currentFlightNumber++;
                    
                    if (currentFlightNumber < totalFlightsToEnter) 
                    {
                        resetForNewFlight();
                        errorText.setString("Entering data for flight " + to_string(currentFlightNumber + 1) + 
                                          " of " + to_string(totalFlightsToEnter));
                        errorText.setFillColor(sf::Color::Red);
                    } 
                    else 
                    {
                        data.complete = true; //complete
                    }
                }
            
            }            
            
            //check for field selection
            for (size_t i = (currentFlightNumber == 0 ? 0 : 1); i < inputBoxes.size(); i++) 
            {
                if (inputBoxes[i].getGlobalBounds().contains(event.mouseButton.x, event.mouseButton.y)) 
                {
                    data.currentField = i;
                }
            }
        }
    }

    bool validateInput() //just take the oroginal validation logic and repeat
    {
        try 
        {
            switch (data.currentField) 
            {
                case 0: 
                { 
                    //number of flights
                    int n = stoi(data.numFlights);
                    if (n <= 0 || n > MAX_ACTIVE_FLIGHTS) 
                    {
                        errorText.setString("Invalid number (1-20)");
                        return false;
                    }
                    break;
                }
                case 1: 
                { 
                    //flight ID
                    if (data.id.empty()) 
                    {
                        errorText.setString("Flight ID cannot be empty");
                        return false;
                    }
                    break;
                }
                case 2: 
                { 
                    //airline name
                    if (data.airline.empty()) 
                    {
                        errorText.setString("Airline name cannot be empty");
                        return false;
                    }
                    break;
                }
                case 3: 
                { 
                    //aircraft Type
                    int type = stoi(data.type);
                    if (type < 0 || type > 2) 
                    {
                        errorText.setString("Aircraft type must be 0, 1, or 2");
                        return false;
                    }
                    break;
                }
                case 4: 
                { 
                    //diirection
                    int dir = stoi(data.direction);
                    if (dir < 0 || dir > 3) 
                    {
                        errorText.setString("Direction must be 0 to 3");
                        return false;
                    }
                    break;
                }
                case 5: 
                { 
                    //priority
                    int prio = stoi(data.priority);
                    if (prio < 1 || prio > 5) 
                    {
                        errorText.setString("Priority must be between 1 and 5");
                        return false;
                    }
                    break;
                }
                case 6: 
                { 
                    //scheduled time
                    int hh, mm;
                    if (sscanf(data.scheduledTime.c_str(), "%d:%d", &hh, &mm) != 2) 
                    {
                        errorText.setString("Invalid format. Use hh:mm");
                        return false;
                    }
                    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) 
                    {
                        errorText.setString("Hours must be 0-23 and minutes 0-59");
                        return false;
                    }
                    break;
                }
                default:
                    errorText.setString("Invalid input field");
                    return false;
            }
        } 
        catch (...) //any sort of error
        {
            errorText.setString("Invalid input");
            return false;
        }
    
        errorText.setString(""); //clear previous error on success
        return true;
    }
    

    string& getCurrentInput() //getter for input
    {
        switch(data.currentField) 
        {
            case 0: return data.numFlights;
            case 1: return data.id;
            case 2: return data.airline;
            case 3: return data.type;
            case 4: return data.direction;
            case 5: return data.priority;
            case 6: return data.scheduledTime;
            default: return data.numFlights;
        }
    }

    void draw(sf::RenderWindow& window) //draw input UI
    {
        //draw green background lines
        for (int i = 0; i < 800; i += 40)
        {
            sf::Vertex line[] =
            {
                sf::Vertex(sf::Vector2f(i, 0), sf::Color(0, 255, 0, 50)), 
                sf::Vertex(sf::Vector2f(0, i), sf::Color(0, 255, 0, 50))
            };
            window.draw(line, 2, sf::Lines);

            sf::Vertex line2[] =
            {
                sf::Vertex(sf::Vector2f(i, 600), sf::Color(0, 255, 0, 50)),
                sf::Vertex(sf::Vector2f(800, i), sf::Color(0, 255, 0, 50))
            };
            window.draw(line2, 2, sf::Lines);
        }


        //highlight current field but hide numbflights after first 
        for (size_t i = (currentFlightNumber == 0 ? 0 : 1); i < inputBoxes.size(); i++) 
        {
            inputBoxes[i].setOutlineColor(i == data.currentField ? 
                sf::Color::Red : sf::Color::Transparent);
            window.draw(prompts[i]);
            window.draw(inputBoxes[i]);
            window.draw(inputTexts[i]);
        }

        //update input texts
        inputTexts[0].setString(data.numFlights);
        inputTexts[1].setString(data.id);
        inputTexts[2].setString(data.airline);
        inputTexts[3].setString(data.type);
        inputTexts[4].setString(data.direction);
        inputTexts[5].setString(data.priority);
        inputTexts[6].setString(data.scheduledTime);

         // Always show submit button
         window.draw(submitButton);
         window.draw(submitText);
         window.draw(errorText);
         
         // Show flight progress
         if (currentFlightNumber > 0) 
         {
             sf::Text progressText("Flight " + to_string(currentFlightNumber) + " of " + 
                                  to_string(totalFlightsToEnter), font, 20);
             progressText.setPosition(50, 20);
             progressText.setFillColor(sf::Color::White);
             window.draw(progressText);
         }
    }

    void resetForNewFlight() //clear fields
    {
        data.id = "";
        data.airline = "";
        data.type = "";
        data.direction = "";
        data.priority = "";
        data.scheduledTime = "";
        data.currentField = 0;
        data.complete = false;
        errorText.setString("");
    }
};

void processInputData(AirControlX& atc, const FlightInputData& data) //when u input the data for a slight, initialize aircraft
{
    //add one aircradft to the atc at a time
        Aircraft ac;
        ac.id = data.id;
        ac.airline = data.airline;
        ac.type = static_cast<AircraftType>(stoi(data.type));
        ac.direction = static_cast<Direction>(stoi(data.direction));
        ac.priority = stoi(data.priority);
        ac.scheduledTimeStr = data.scheduledTime;

        //parse time
        int hh, mm;
        sscanf(data.scheduledTime.c_str(), "%d:%d", &hh, &mm);
        ac.scheduledMinutes = hh * 60 + mm;

        //set other defaults (module 2 wala code)
        ac.phase = (ac.direction == NORTH || ac.direction == SOUTH) ? HOLDING : AT_GATE;
        ac.currentSpeed = (ac.phase == HOLDING) ? 400 + (rand() % 201) : 0;
        ac.isEmergency = (ac.type == EMERGENCY);
        ac.hasAVN = false;
        ac.hasFault = false;
        ac.lastPhaseChange = time(nullptr);
        ac.queueEntryTime = time(nullptr);
        ac.fuelPercentage = (ac.direction == NORTH || ac.direction == SOUTH) ? 
            (70 + (rand() % 31)) : 100.0;  // b/w 70-100 for arrivals, 100 for departures
        ac.hadLowFuel = false;

        atc.flights.push_back(ac);
}

//simulation time
class SimulationVisualizer 
{
    private:
        sf::Font font;
        sf::Font font2;
        sf::RectangleShape runways[3];
        vector<sf::CircleShape> flightDots;
        vector<sf::Text> flightLabels; 
        vector<sf::RectangleShape> queueBoxes;
        vector<sf::Text> consoleTexts;
        sf::RectangleShape consoleArea;
        int maxConsoleLines = 15;
        sf::Text runwayLabels[3];
        sf::Text statusHeader;
        vector<sf::Text> statusTexts;
        sf::Text simulationTimeText;
        int consoleX = 50, consoleY = 10; //store console coordintes to easily display output
        int incY = 5; //the number to increment consoleY by for each consecutive output string
        int runwayStartX = 380, runwayStartY = 320;
        sf::Clock messageClock; //for clearing the stupid message
        float messageDuration = 3.0f; // seconds
        const int textLineHeight = 20; //inc for equalish spacing
        vector<sf::Text> queueLabels;
        sf::Text currentMessage;

        
    public:
        SimulationVisualizer() 
        {
            if (!font.loadFromFile("Textures/VT323-Regular.ttf")) 
            {
                cerr << "Error: Failed to load font for simulation.\n";
                return;
            }
            if (!font2.loadFromFile("Textures/PixelatedEleganceRegular-ovyAA.ttf")) 
            {
                cerr << "Error: Failed to load font for simulation.\n";
                return;
            }
            
            //setup console area (bottom right)
            consoleArea.setSize(sf::Vector2f(550, 700));
            consoleArea.setPosition(0, consoleY); //start at 0, center text at consoleX (50)
            consoleArea.setFillColor(sf::Color(0, 50, 0, 200)); //dark green semi-transparent

            //setup simulation time display
            simulationTimeText.setFont(font);
            simulationTimeText.setCharacterSize(19);
            simulationTimeText.setPosition(consoleX, consoleY + incY);
            incY += 20;
            simulationTimeText.setFillColor(sf::Color::Yellow); //more prominent            
            
            //status header in console
            statusHeader.setString("====== AirControlX Status ======");
            statusHeader.setFont(font);
            statusHeader.setCharacterSize(16);
            statusHeader.setPosition(consoleX, consoleY + incY);
            incY += 5;
            statusHeader.setFillColor(sf::Color::White);
            
            //setup runways (3 rectangles)
            for (int i = 0; i < 3; i++) 
            {
                runways[i].setSize(sf::Vector2f(400, 40));
                runways[i].setOutlineThickness(2);
                runways[i].setOutlineColor(sf::Color::White);
                runways[i].setPosition(runwayStartX, runwayStartY + i * 80);
            }
            runways[0].setFillColor(sf::Color(115, 147, 179)); //a: blue
            runways[1].setFillColor(sf::Color(170, 74, 68)); //b: red
            runways[2].setFillColor(sf::Color(225, 193, 110)); //c: yellow

            
            //setup queue boxes and runways
            for (int i = 0; i < 3; i++) 
            {
                sf::RectangleShape box(sf::Vector2f(150, 100));
                box.setPosition(620, 30 + i*80);
                box.setFillColor(sf::Color(0, 0, 0, 150));
                box.setOutlineThickness(1);
                box.setOutlineColor(sf::Color::White);
                queueBoxes.push_back(box);

                sf::Text label;
                label.setFont(font);
                label.setCharacterSize(14);
                label.setFillColor(sf::Color::White);
                label.setString(i == 0 ? "Arrival Q" : i == 1 ? "Departure Q" : "Cargo/Emerg Q");
                label.setPosition(620, 30 + i*80 - 20);  // Slightly above each queue box
                queueLabels.push_back(label);


                 //runway labels
                runwayLabels[i].setFont(font);
                runwayLabels[i].setCharacterSize(16);
                runwayLabels[i].setPosition(runwayStartX + 150, runwayStartY + i * 80);
                runwayLabels[i].setFillColor(sf::Color::Black);
            }

            runwayLabels[0].setString("Arrivals");
            runwayLabels[1].setString("Departures");
            runwayLabels[2].setString("Cargo/Emerg");

            currentMessage.setFont(font2);
            currentMessage.setCharacterSize(12);
            currentMessage.setPosition(10, 550);
            currentMessage.setFillColor(sf::Color(0, 255, 127)); //spring green

        }
        
        void update(const AirControlX& atc) //, const vector<string>& consoleOutput) 
        {
            //clear previous
            flightDots.clear();
            flightLabels.clear();
            statusTexts.clear();
            consoleTexts.clear();

            //update simulation time
            stringstream timeSS;
            timeSS << "Simulation Time: " << atc.simulationTime << "/" << SIMULATION_DURATION << " seconds";
            simulationTimeText.setString(timeSS.str());

             // update runway status
             int textY = consoleY + 40;
             statusTexts.push_back(createText("=== Runways ===", consoleX, textY));
             textY += textLineHeight;
            for (auto& runway : atc.runways) 
            {
                string status = runway.getName() + ": ";
                if (runway.isOccupied && runway.currentAircraft) 
                {
                    status += runway.currentAircraft->id + " (" + runway.currentAircraft->getPhaseString() + ")";
                } 
                else 
                {
                    status += "Available";
                }
                sf::Text text(status, font, 16);
                text.setPosition(consoleX + 10, textY);
                text.setFillColor(sf::Color::White);
                statusTexts.push_back(text);
                textY += textLineHeight;
            }
        
            textY += textLineHeight/2;
            statusTexts.push_back(createText("=== Active Flights ===", consoleX, textY));
            textY += textLineHeight;
            
            sf::Text header("Flight ID  Type        Phase      Speed  Priority  Runway    AVN  Wait(s) Fuel(%)", font, 16);
            header.setPosition(consoleX + 10, textY);
            header.setFillColor(sf::Color::White);
            statusTexts.push_back(header);
            textY += textLineHeight;
            
            //update flight details
            for (auto& flight : atc.flights) 
            {
                stringstream flightSS;
                flightSS << left << setw(10) << flight.id.substr(0, 9)
                << setw(12) << flight.getTypeString().substr(0, 11)
                << setw(12) << (flight.hasFault ? "TOWED" : flight.getPhaseString().substr(0, 11))
                << setw(8) << fixed << setprecision(2) << flight.currentSpeed
                << setw(10) << flight.priority
                << setw(10) << flight.getRunwayString()
                << setw(5) << flight.AVNcount //old: (flight.hasAVN ? "Yes" : "No")
                << setw(8) << fixed << setprecision(2) << flight.waitTime
                << setw(10) << fixed << setprecision(2) << flight.fuelPercentage;
                                    
                sf::Text text(flightSS.str(), font, 16);
                text.setPosition(consoleX + 10, textY);
                text.setFillColor(flight.isEmergency ? sf::Color::Red : sf::Color::White); //cahnge color
                statusTexts.push_back(text);
                textY += textLineHeight;
            }
            
            //lock display mutex to ensure consistent state cause we r about to access runways and flights
            lock_guard<mutex> lock(atc.displayMutex);

            //runway aircraft + labels
            for (int i = 0; i < 3; i++) 
            {
                if (atc.runways[i].currentAircraft != nullptr) //not using LOCK, but visualize on runway
                {
                    sf::CircleShape dot(10);
                    dot.setPosition(runways[i].getPosition().x + 20, 
                                runways[i].getPosition().y + 10);
                    
                    //color coding based on runway
                    if (i == 0) dot.setFillColor(sf::Color::Blue);    // RWY-A
                    else if (i == 1) dot.setFillColor(sf::Color::Red); // RWY-B
                    else dot.setFillColor(sf::Color::Magenta); // RWY-C magenta to pop
                    
                    flightDots.push_back(dot);

                   //create label for aircraft (ID and speed)
                    if (atc.runways[i].currentAircraft)
                    {
                        string labelText = atc.runways[i].currentAircraft->id + "\n" + to_string((int)atc.runways[i].currentAircraft->currentSpeed) + "km/h";
                        
                        sf::Text label;
                        label.setFont(font); 
                        label.setString(labelText);
                        label.setCharacterSize(16);
                        label.setPosition(dot.getPosition().x + 10, dot.getPosition().y + 10);
                        label.setFillColor(sf::Color::Green);
                        flightLabels.push_back(label);
                    }  
                }
            }
            
            // Update queue aircraft
            vector<vector<const Aircraft*>> queuedFlights(3);
        
            //get flights from each queue
            {                
                lock_guard<mutex> lock1(atc.arrivalQueueMutex);
                lock_guard<mutex> lock2(atc.departureQueueMutex);
                lock_guard<mutex> lock3(atc.cargoQueueMutex); 
                            
                
                auto tempArrival = atc.arrivalQueue;
                while (!tempArrival.empty())
                {
                    queuedFlights[RWY_A].push_back(tempArrival.top());
                    tempArrival.pop();
                }
                
                auto tempDeparture = atc.departureQueue;
                while (!tempDeparture.empty()) 
                {
                    queuedFlights[RWY_B].push_back(tempDeparture.top());
                    tempDeparture.pop();
                }
                
                auto tempCargo = atc.cargoEmergencyQueue;
                while (!tempCargo.empty()) 
                {
                    queuedFlights[RWY_C].push_back(tempCargo.top());
                    tempCargo.pop();
                }
            }
        
            //create dots and labels in queue boxes
            for (int runwayIdx = 0; runwayIdx < 3; runwayIdx++)
            {
                const auto& queue = queuedFlights[runwayIdx];
                const float boxWidth = queueBoxes[runwayIdx].getSize().x;
                const float boxHeight = queueBoxes[runwayIdx].getSize().y;
                const float startX = queueBoxes[runwayIdx].getPosition().x + 10;
                const float startY = queueBoxes[runwayIdx].getPosition().y + 15;

                //keep track of flight dots so they dont overflow
                const int maxDotsPerRow = 5;  
                const float dotSpacing = 25; 
                const float dotSize = 8;   
                
                for (size_t i = 0; i < queue.size(); i++) 
                {
                    const auto& flight = queue[i];
                    if (flight->hasFault) continue; //skip drawing towed flights

                    //check if flight is already on a runway
                    bool isOnRunway = false;
                    for (const auto& runway : atc.runways) 
                    {
                        if (runway.currentAircraft == flight ||
                            (flight->phase == LANDING || flight->phase == TAKEOFF_ROLL || flight->phase == TAXI))
                        {
                            isOnRunway = true;
                            break;
                        }
                    }
                    if (isOnRunway) continue; //skip flights already on a runway

                     //calculate position so they start "wrapping around" growing leftwards
                    int row = i / maxDotsPerRow;
                    int col = i % maxDotsPerRow;
                    sf::CircleShape dot(dotSize);
                    dot.setPosition(startX + col * dotSpacing, startY + row * dotSpacing);
                    
                    //color code by runway
                    if (runwayIdx == 0) dot.setFillColor(sf::Color::Blue);
                    else if (runwayIdx == 1) dot.setFillColor(sf::Color::Red);
                    else dot.setFillColor(sf::Color::Magenta);
                    
                    flightDots.push_back(dot);
                }
            }

           currentMessage.setString(atc.getLatestMessage()); //get latest msg to output only
        }

        sf::Text createText(const string& str, float x, float y) //helper function to create and set positon of text
        {
            sf::Text text(str, font, 16);
            text.setPosition(x, y);
            text.setFillColor(sf::Color::White);
            return text;
        }
        
        void draw(sf::RenderWindow& window) 
        {
            
            //draw queue boxes
            for (auto& box : queueBoxes) 
            {
                window.draw(box);
            }
            
            //draw console area and text
            window.draw(consoleArea);
            window.draw(statusHeader);
            window.draw(simulationTimeText);
            window.draw(currentMessage);


             //draw runways later so they r on top of the console
             for (auto& runway : runways) 
             {
                 window.draw(runway);
             }

             //draw queue and runway labels
            for (int i = 0; i < 3; i++) 
            {
                window.draw(runwayLabels[i]);
            }
            for (const auto& label : queueLabels)
            {
                window.draw(label);
            }

            //draw flight dots at the end so they r on top of evetryhing
            for (auto& dot : flightDots) 
            {
                window.draw(dot);
            }
            for (auto & label : flightLabels) //put labels on top too
            {
                window.draw(label);
            } 

            //except dfor the texts - those r even higher
            for (auto& textS : statusTexts) 
            {
                window.draw(textS);
            }
        }
};

int main() 
{
    srand(time(nullptr));
    AirControlX atc;

    // --------------------- AVN GENERATOR PROCESS CODE --------------------
    pid_t pid;
    // Setup pipe
    if (pipe(atc.pipe_fd) == -1) 
    {
        std::cerr << "[ATC] Failed to create pipe" << std::endl;
        return 1;
    }

    // Fork AVN Generator
    pid = fork();
    if (pid < 0) 
    {
        std::cerr << "[ATC] Fork failed" << std::endl;
        return 1;
    }
    else if (pid == 0) 
    {
        // Child Process: AVN Generator

        // Redirect pipe read end to stdin
        close(atc.pipe_fd[1]); // Close write end

        if (dup2(atc.pipe_fd[0], STDIN_FILENO) == -1) 
        {
            std::cerr << "[AVN Generator] Failed to redirect stdin" << std::endl;
            return 1;
        }

        close(atc.pipe_fd[0]); // Close read end

        execl("./avn_generator", "./avn_generator", (char*)NULL);
        std::cerr << "[AVN Generator] execl failed" << std::endl;
        return 1;
    }

    // Parent continues
    close(atc.pipe_fd[0]); // Close read end in parent (only writing to pipe)


    // --------------GRAPHICAL SIMULATION CODE------------------

    //initialize window
    sf::RenderWindow window(sf::VideoMode(resolutionX, resolutionY), "AirControlX Simulation", sf::Style::Close | sf::Style::Titlebar);
	window.setPosition(sf::Vector2i(100, 100)); //choose launch point of screen

    //initializine bg
	sf::Texture backgroundTexture;
	sf::Sprite backgroundSprite;
    if (!backgroundTexture.loadFromFile("Textures/gridBackground.png")) 
    {
        cerr << "Error: Failed to load background texture.\n";
        return -1;
    }
	backgroundSprite.setTexture(backgroundTexture);
	backgroundSprite.setColor(sf::Color(255, 255, 255, 255 * 0.50)); //reduces opacity to 50%

    //font
    sf::Font font;
    if (!font.loadFromFile("Textures/PixelatedEleganceRegular-ovyAA.ttf"))
    {
        cerr << "Error: Failed to font.\n";
        return -1;
    }

    bool inputComplete = false; //track input to switch to simulation
    bool simulationStarted = false;
    InputHandler handleInput;
    SimulationVisualizer simulation;
    while (window.isOpen()) 
	{
		window.draw(backgroundSprite);

        sf::Event e;
        while (window.pollEvent(e)) 
		{
            
			if (e.type == sf::Event::Closed)
			{
				return 0;
			}

            if (!inputComplete) //handle whatever was inputted
                handleInput.handleEvent(e);
			
		}

        window.clear();	

        window.draw(backgroundSprite);


        if (!inputComplete) 
        {
            handleInput.draw(window); //draw ui
            if (handleInput.data.complete) //all flight data entered
            {
                for (auto& flightData : allFlightInputs)
                    processInputData(atc, flightData); //send data to atc per flight
                
                inputComplete = true; //start simulation instead
                
                window.clear();

                //mimic sleep for aabia ma'am
                sf::Clock delayClock;
                while (delayClock.getElapsedTime().asSeconds() < 4.0f)
                 {
                    sf::Text switchText("Simulation starting....", font, 30);
                    switchText.setPosition(10, 250);
                    switchText.setFillColor(sf::Color::Green);
                    window.clear(backgroundSprite.getColor());
                    window.draw(backgroundSprite);
                    window.draw(switchText);
                    window.display();
                }

            }
        }
       else
       {
            if (!simulationStarted) 
            {
                simulationStarted = true;
                atc.mapScheduledTimes();

                atc.scheduleFlights();
                // Start simulation in a separate thread
                thread([&]() {
                    atc.startSimulation();
                }).detach();
            }
            simulation.update(atc);
            simulation.draw(window);

            static bool shownCompleteMessage = false; //because adding the child makes this auto close
            if (simulationStarted && atc.simulationComplete) 
            {

                //show the completion message for 5 seconds
                sf::Clock waitClock;
                while (waitClock.getElapsedTime().asSeconds() < 5.0f && window.isOpen()) 
                {
                    sf::Event event;
                    while (window.pollEvent(event)) 
                    {
                        if (event.type == sf::Event::Closed)
                            window.close();
                    }

                    window.clear();
                    window.draw(backgroundSprite);
                
                    //show completion message
                    sf::Text completionText("Simulation Complete!", font, 30);
                    completionText.setPosition(10, 250);
                    completionText.setFillColor(sf::Color::Green);
                    window.draw(completionText);
                    //show summary message which is always the last atc message
                    sf::Text summaryText(atc.getLatestMessage(), font, 15);
                    summaryText.setPosition(10, 290);
                    summaryText.setFillColor(sf::Color::White);
                    window.draw(summaryText);

                    window.display();

                }
            }
        
        }

        window.display();
        window.clear();

    }

    return 0;
}

