find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(tst_receiver tst_receiver.cpp)
target_link_libraries(tst_receiver PRIVATE denon-remote-core Qt6::Test)

# Built in rather than read from disk, so the test runs from any build
# directory, the Debian package build's included.
qt_add_resources(tst_receiver "captures"
    PREFIX "/"
    FILES
        data/AVC-X3700H-Deviceinfo-8080.xml
        data/AVR-1912-Deviceinfo.xml
        data/AVR-X2000-Deviceinfo.xml
        data/AVR-1912-formMainZone_MainZoneXml.xml
        data/AVR-3311CI-formMainZone_MainZoneXml.xml
        data/AVR-4810-formMainZone_MainZoneXml.xml
        data/AVR-X2100W-formMainZone_MainZoneXml.xml
        data/SR6011-formMainZone_MainZoneXmlStatus.xml
)

add_test(NAME receiver COMMAND tst_receiver)
