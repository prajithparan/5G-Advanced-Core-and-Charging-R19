# E1 (Subscriber)/E10 (Account) real PostgreSQL persistence -- schema.sql's own header explains
# the scoping: store library only this turn, no HTTP/REST service yet (CHARGING_PROMPT.md's P4.7
# owns the full BSS-layer service surface). Real TMF632 Individual/Organization persistence
# (bss_sid, libs/bss-sid) plus this project's own Account/Subscriber linking tables.
find_package(libpqxx CONFIG REQUIRED)

add_library(subscriber_management_store STATIC src/store.cpp)
target_link_libraries(subscriber_management_store PUBLIC bss_sid libpqxx::pqxx)
target_include_directories(subscriber_management_store PUBLIC src)
