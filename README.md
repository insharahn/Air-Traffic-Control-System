# AirControlX

AirControlX is a modular, real-time air traffic control simulation built in C++ that handles flight management, runway scheduling, aviation violation monitoring, and payment processing. The project features multiple processes, inter-process communication, multithreading, fault simulation, and a graphical interface built using SFML.

There are 3 runways, each with a specific purpose (departures/arrivals/emergency or cargo). Only one flight can access a runway at a time. The flights move through phases based on their direction. Airfracts with ground violations are towed. Speed violations are constantly checked and an Aircraft Violation Notice (AVN) is generated if detected, which can be viewed on the Airline Portal and paid on the Stripe Payment Portal. 

## Features

- Modular architecture with 4 main components:
  1. atc_controller.cpp – Manages all air traffic and runways.
  2. avn_generator.cpp – Generates and logs Aviation Notices (AVNs).
  3. airline_portal.cpp – Interface for querying AVN history and status.
  4. stripe_pay.cpp – Simulated payment system for AVN fines.
- Inter-process communication using named pipes (FIFOs).
- Realistic flight phase simulation with speed and fuel monitoring.
- AVN issuance based on speed violations per flight phase.
- Emergency handling for low fuel and ground faults.
- SFML-based GUI for air traffic input and live runway visualization.
- Console-based interfaces for Airline Portal and StripePay.
- Thread-safe operations using mutexes, condition variables, and atomic variables.
- Log files for AVN history and system events.

## Data Structures

- Structs/Classes for Aircraft, AVN, Runways, and others ensure organized and consistent data handling.
- Priority Queues for flight scheduling (Arrival, Departure, Emergency).
- Hash Maps for efficient AVN lookups.
- Vectors for AVN history and pending fines.

## Flight Simulation & Runway Management

- Flights progress through defined phases based on whether they are arrival or departure flights and phase transitions are time and resource-dependent.
- Three distinct runways:
  1. RWY-A: Arrivals (N/S)
  2. RWY-B: Departures (E/W)
  3. RWY-C: Cargo/Emergencies
- Speed monitoring enforces phase-specific limits and issues AVNs accordingly.
- Low fuel and faults are handled dynamically via emergency redirection.

## User Interfaces

### Graphical (SFML)
- Input screen for new flight details.
- Live simulation screen with queues, runways, logs, and flight states.

### Console
- Airline Portal: Search AVNs by Flight ID and date.
- StripePay: View/pay pending fines.

## Synchronization
- **Multithreading:**
  - Flight threads (1 per flight to proceed through the phases)
  - Radar threads (1 per flight to check violations)
  - Display thread (UI updates)
- **Mutexes & Condition Variables:**
  - Protect shared resources (queues, logs, runways).
- **Atomic Variables:**
  - Control simulation state and time updates.

## Compilation & Execution
### Dependencies

- C++17 or later
- <a href=https://www.sfml-dev.org/download/sfml/2.6.1/>SFML 2.6.1</a>
- POSIX-compliant environment (for FIFOs and fork())

### Compilation

Run the following commands:
``` sh
g++ -o atc_controller atc_controller.cpp -lsfml-graphics -lsfml-window -lsfml-system -lpthread
g++ -o avn_generator avn_generator.cpp -lpthread
g++ -o airline_portal airline_portal.cpp -lpthread
g++ -o stripe_pay stripe_pay.cpp -lpthread
```

### Running the Project
Run each component in a separate terminal window except for the avn_generator object file.

The main component (atc_controller) should run first:
``` sh
./atc_controller
```
Then the Airline and Stripe Payment Portals should be launched in seperate terminals. 
``` sh
./airline_portal
```
``` sh
./stripe_pay
```

# Contributers

- <a href=https://github.com/insharahn>Insharah Irfan Nazir</a>
- <a href=https://github.com/AabiaAli>Aabia Ali</a>
