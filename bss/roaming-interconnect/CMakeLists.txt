# E7 (Roaming and Interconnect Agreements) real PostgreSQL persistence -- schema.sql's own header
# explains the scoping: store library only this turn, no HTTP/REST service yet
# (CHARGING_PROMPT.md's P4.11 owns the full BSS-layer service surface). Real TMF651 Agreement
# persistence (bss_sid, libs/bss-sid) plus this project's own InterconnectAgreement/RoamingCdrFile
# linking tables.
find_package(libpqxx CONFIG REQUIRED)

add_library(roaming_interconnect_store STATIC src/store.cpp)
target_link_libraries(roaming_interconnect_store PUBLIC bss_sid libpqxx::pqxx)
target_include_directories(roaming_interconnect_store PUBLIC src)
