idf_component_register(SRCS "frame_analyzer.c" "frame_analyzer_parser.c"
                    INCLUDE_DIRS "interface"
                    REQUIRES wifi_controller esp_event esp_wifi)