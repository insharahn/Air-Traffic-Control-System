#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h> // For sleep() kyunke I always forget
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>
#include <string.h>
#include <ctype.h>

// macros
#define MAX_AIRCRAFTS_PER_AIRLINE 10
#define MAX_RUNWAYS 3
#define MAX_ACTIVE_FLIGHTS 20
#define MAX_AIRLINES 6
#define MAX_AIRLINE_NAME 50
#define MAX_FLIGHT_ID 20
#define MAX_VIOLATION_MSG 100


// Enums
// Basically to assign constant values to everything so like COMMERCIAL + 1 will be equal to CARGO
// Used for data that we know we won't change like directions, days of the week, months, etc.
typedef enum { COMMERCIAL, CARGO, EMERGENCY } AircraftType;
typedef enum { HOLDING, APPROACH, LANDING, TAXI, AT_GATE, TAKEOFF_ROLL, CLIMB, CRUISE } FlightPhase;
typedef enum { RWY_A, RWY_B, RWY_C } RunwayID;
typedef enum { NORTH, SOUTH, EAST, WEST } Direction;

// Structs
typedef struct {
    double minSpeed;           // km/h
    double maxSpeed;          // km/h
    char violationCriteria[MAX_VIOLATION_MSG]; // C-style string for violation description
} SpeedRule;

typedef struct {
    char id[MAX_FLIGHT_ID];              // e.g., "PIA001"
    AircraftType type;
    FlightPhase phase;
    double currentSpeed;      // km/h
    int isEmergency;         // 0 (false) or 1 (true)
    Direction direction;
    int hasAVN;              // 0 (false) or 1 (true)
    RunwayID assignedRunway;
    ////////////////////////////////////////////
    int hasFault; 		// 0 (false) or 1 (true)
    int priority;		
} Aircraft;

typedef struct {
    char name[MAX_AIRLINE_NAME];
    AircraftType type;
    int totalAircrafts;
    int activeFlights;
    Aircraft aircrafts[MAX_AIRCRAFTS_PER_AIRLINE];   // Max 10 aircrafts per airline (more than needed)
} Airline;

typedef struct {
    RunwayID id;
    int isOccupied;          // 0 (false) or 1 (true) - leaving the flag just in case it's needed during scheduling
    pthread_mutex_t lock; //runway lock to let one flight (aircraft) use the runway at a time
    Aircraft * currentAircraft; //track what aircraft is on this runway
    
} Runway;

typedef struct {
    Airline airlines[MAX_AIRLINES];      // 6 airlines
    Runway runways[MAX_RUNWAYS];       // 3 runways
    double simulationTime;   // 300 seconds (5 minutes)
    Aircraft* activeFlights[MAX_ACTIVE_FLIGHTS]; // Max 20 active flights
    int activeFlightCount;
} AirControlX;

// Runway Functions
void Runway_init(Runway* runway, RunwayID id) {
    runway->id = id;
    runway->isOccupied = 0;
    runway->currentAircraft = NULL;
    pthread_mutex_init(&runway->lock, NULL);
}

int Runway_isRunwayValid(Runway* runway, Aircraft* aircraft) {
    if (runway->id == RWY_A && (aircraft->direction == NORTH || aircraft->direction == SOUTH))
        return 1; // RWY-A for arrivals
    if (runway->id == RWY_B && (aircraft->direction == EAST || aircraft->direction == WEST))
        return 1; // RWY-B for departures
    if (runway->id == RWY_C && (aircraft->type == CARGO || aircraft->isEmergency))
        return 1; // RWY-C for cargo/emergency
    return 0;
}

int Runway_assignAircraft(Runway* runway, Aircraft* aircraft) {
    if (!runway->isOccupied && Runway_isRunwayValid(runway, aircraft)) {
        runway->currentAircraft = aircraft;
        runway->isOccupied = 1;
        aircraft->assignedRunway = runway->id;
        return 1;
    }
    return 0;
}

int Runway_getRunwayIndex(RunwayID id) //helper function to return index based on id
{
    switch (id) 
    {
        case RWY_A: return 0;
        case RWY_B: return 1;
        case RWY_C: return 2;
        default: return -1;
    }
}

void Runway_releaseRunway(Runway* runway) {
    runway->isOccupied = 0;
    runway->currentAircraft = NULL;
}

// AirControlX Functions
void AirControlX_init(AirControlX* atc) {
    atc->simulationTime = 300.0;
    atc->activeFlightCount = 0;
    

    // Initialize airlines
    Airline airlineData[6] = {
        {"PIA", COMMERCIAL, 6, 4, {}},
        {"AirBlue", COMMERCIAL, 4, 4, {}},
        {"FedEx", CARGO, 3, 2, {}},
        {"Pakistan Airforce", EMERGENCY, 2, 1, {}},
        {"Blue Dart", CARGO, 2, 2, {}},
        {"AghaKhan Air Ambulance", EMERGENCY, 2, 1, {}}
    };

    for (int i = 0; i < MAX_AIRLINES; i++) {
        atc->airlines[i] = airlineData[i];
        for (int j = 0; j < atc->airlines[i].activeFlights; j++) {
            Aircraft* aircraft = &atc->airlines[i].aircrafts[j];
            snprintf(aircraft->id, MAX_FLIGHT_ID, "%s%d", atc->airlines[i].name, j + 1);
            aircraft->type = atc->airlines[i].type;
          //  aircraft->phase = (aircraft->type == COMMERCIAL) ? HOLDING : AT_GATE;
            aircraft->currentSpeed = 0.0;
            aircraft->isEmergency = (rand() % 100 < 15); // 15% chance
            aircraft->hasAVN = 0;
           // aircraft->direction = (aircraft->type == CARGO) ? SOUTH : (rand() % 4); // NORTH=0, SOUTH=1, EAST=2, WEST=3

            if (aircraft->type == COMMERCIAL) {
                aircraft->direction = rand() % 2; // 0 or 1 → NORTH or SOUTH
                aircraft->phase = HOLDING;
            } else if (aircraft->type == CARGO) {
                aircraft->direction = SOUTH;
                aircraft->phase = AT_GATE;
            } else { // EMERGENCY
                aircraft->direction = rand() % 4;
                aircraft->phase = AT_GATE;
            }

            aircraft->assignedRunway = -1;  // no runway yet
            atc->activeFlights[atc->activeFlightCount++] = aircraft;
            aircraft->assignedRunway = -1;  // invalid value, so assignment logic works

        }
    }

    // Initialize runways
    Runway_init(&atc->runways[0], RWY_A);
    Runway_init(&atc->runways[1], RWY_B);
    Runway_init(&atc->runways[2], RWY_C);
}

SpeedRule AirControlX_getSpeedRule(FlightPhase phase) {
    SpeedRule rule;
    switch (phase) {
        case HOLDING:
            rule.minSpeed = 400;
            rule.maxSpeed = 600;
            snprintf(rule.violationCriteria, 100, "Speed exceeds 600 km/h");
            break;
        case APPROACH:
            rule.minSpeed = 240;
            rule.maxSpeed = 290;
            snprintf(rule.violationCriteria, 100, "Speed below 240 or above 290 km/h");
            break;
        case LANDING:
            rule.minSpeed = 30;
            rule.maxSpeed = 240;
            snprintf(rule.violationCriteria, 100, "Speed exceeds 240 or fails to slow below 30 km/h");
            break;
        case TAXI:
            rule.minSpeed = 15;
            rule.maxSpeed = 30;
            snprintf(rule.violationCriteria, 100, "Speed exceeds 30 km/h");
            break;
        case AT_GATE:
            rule.minSpeed = 0;
            rule.maxSpeed = 5;
            snprintf(rule.violationCriteria, 100, "Speed exceeds 10 km/h");
            break;
        case TAKEOFF_ROLL:
            rule.minSpeed = 0;
            rule.maxSpeed = 290;
            snprintf(rule.violationCriteria, 100, "Speed exceeds 290 km/h");
            break;
        case CLIMB:
            rule.minSpeed = 250;
            rule.maxSpeed = 463;
            snprintf(rule.violationCriteria, 100, "Speed exceeds 463 km/h");
            break;
        case CRUISE:
            rule.minSpeed = 800;
            rule.maxSpeed = 900;
            snprintf(rule.violationCriteria, 100, "Speed below 800 or above 900 km/h");
            break;
        default:
            rule.minSpeed = 0;
            rule.maxSpeed = 0;
            snprintf(rule.violationCriteria, 100, "Unknown phase");
            break;
    }
    return rule;
}

void AirControlX_monitorSpeed(Aircraft* aircraft) {
    SpeedRule rule = AirControlX_getSpeedRule(aircraft->phase);
    if (aircraft->currentSpeed < rule.minSpeed || aircraft->currentSpeed > rule.maxSpeed) {
        aircraft->hasAVN = 1;
        printf("AVN Issued for %s: %s (%.2f km/h)\n",
               aircraft->id, rule.violationCriteria, aircraft->currentSpeed);
    }
}

void AirControlX_updateFlightPhase(AirControlX* atc, Aircraft* aircraft)
{
    //let arrivals hold/approach without a runway (in the air)
    if ((aircraft->direction == NORTH || aircraft->direction == SOUTH) &&
        (aircraft->phase == HOLDING || aircraft->phase == APPROACH)) 
        {

        if (aircraft->phase == HOLDING) 
        {
            aircraft->currentSpeed = 400 + (rand() % 201);  // 400–600 km/h
            if (rand() % 100 < 30) aircraft->phase = APPROACH;
        } 
        else if (aircraft->phase == APPROACH) 
        {
            aircraft->currentSpeed = 240 + (rand() % 51); // 240–290 km/h
            if (rand() % 100 < 30) aircraft->phase = LANDING;
        }
        return;  //don't continue to runway stuff for now
    }
    
    //everything below this point needs a runway
    
    //get runway and lock it
    int runwayIndex = Runway_getRunwayIndex(aircraft->assignedRunway);
    Runway* runway = &atc->runways[runwayIndex];
    pthread_mutex_lock(&runway->lock);

    // ------------------------------------------
    // ---------------- ARRIVALS ----------------
    // ------------------------------------------
    if (aircraft->direction == NORTH || aircraft->direction == SOUTH) 
    {
        switch (aircraft->phase) 
        {
            case LANDING:
                aircraft->currentSpeed -= 30 + (rand() % 50); // slow down from 240 km/h to 30 km/h
                if (aircraft->currentSpeed <= 30) //only taxi if speed below 30 
                {
                    aircraft->phase = TAXI;
                    aircraft->currentSpeed = 15 + (rand() % 16);
                }
                break;

            case TAXI:
                if (rand() % 100 < 40) 
                {
                    aircraft->phase = AT_GATE;
                    aircraft->currentSpeed = 0; //stationary at gate
                    Runway_releaseRunway(runway); //release the aircraft from this runway
                }
                break;

            default:
                break;
        }
    }

    // ------------------------------------------
    // ---------------- DEPARTURES --------------
    // ------------------------------------------
    else if (aircraft->direction == EAST || aircraft->direction == WEST)
    {
        switch (aircraft->phase)
        {
            case AT_GATE:
                if (rand() % 100 < 30)
                {
                    aircraft->phase = TAXI;
                    aircraft->currentSpeed = 15 + (rand() % 16); //raise speed
                }
                break;

            case TAXI:
                if (rand() % 100 < 30)
                {
                    aircraft->phase = TAKEOFF_ROLL;
                    aircraft->currentSpeed = 0; //drop to 0 before takeoff roll
                }
                break;

            case TAKEOFF_ROLL:
                aircraft->currentSpeed += 30 + (rand() % 200); //accelerate rapidly
                if (aircraft->currentSpeed >= 250) //start climbing at 250
                {
                    aircraft->phase = CLIMB;
                    aircraft->currentSpeed = 250 + (rand() % 214); // 250–463 for climb
                    Runway_releaseRunway(runway); //takeoff == release runway
                }
                break;

            case CLIMB:
                if (rand() % 100 < 30) 
                {
                    aircraft->phase = CRUISE; 
                    aircraft->currentSpeed = 800 + (rand() % 101); //cruise at 800 - 900 km/h
                }
                break;

            default:
                break;
        }
    }

    pthread_mutex_unlock(&runway->lock); //unlock the runway for use again
}


void AirControlX_assignRunway(AirControlX* atc, Aircraft* aircraft) 
{
    if (aircraft->assignedRunway != RWY_A && aircraft->assignedRunway != RWY_B && aircraft->assignedRunway != RWY_C) //only assign runway if it's not already assigned
    {
        for (int i = 0; i < MAX_RUNWAYS; i++) {
            if (Runway_assignAircraft(&atc->runways[i], aircraft)) {
                printf("%s assigned to %s\n", aircraft->id,
                       (atc->runways[i].id == RWY_A) ? "RWY-A" :
                       (atc->runways[i].id == RWY_B) ? "RWY-B" : "RWY-C");
                break;
            }
        }
    }
}


void AirControlX_simulate(AirControlX* atc) {
    printf("Starting AirControlX Simulation (5 minutes)\n");
    double elapsedTime = 0;

    while (elapsedTime < atc->simulationTime) {
        for (int i = 0; i < atc->activeFlightCount; i++) {
            Aircraft* aircraft = atc->activeFlights[i];
            AirControlX_assignRunway(atc, aircraft);
            AirControlX_updateFlightPhase(atc, aircraft);
            AirControlX_monitorSpeed(aircraft);
        }
        elapsedTime += 1;
        sleep(1); // 1-second tick
        printf("Time: %.0f seconds\n", elapsedTime);
    }
    printf("Simulation Complete\n");
}

int main() {
    srand(time(NULL));
    AirControlX atc;
    AirControlX_init(&atc);
    AirControlX_simulate(&atc);
    
    for (int i = 0; i < MAX_RUNWAYS; i++) //destroy locks
      pthread_mutex_destroy(&atc.runways[i].lock);
    return 0;
}
