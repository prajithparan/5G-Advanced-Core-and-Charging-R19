# sbi-loadgen: the load-generation harness ADR-0238 named as step (3) toward a real, evidence-based
# performance comparison. Links sbi_core deliberately -- it must drive the SAME production HTTP/2
# client path (ADR-0241's handle pool, real TLS 1.3 + mTLS) the NFs themselves use, not a separate
# benchmark-only transport that would measure something no NF actually runs.
add_executable(sbi-loadgen src/main.cpp)
target_link_libraries(sbi-loadgen PRIVATE sbi_core)
