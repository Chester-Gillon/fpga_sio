# Build the programs for performing QSFP management, for designs which test transceivers

project (qsfp_management C)

add_executable (qsfp_management_menu "qsfp_management_menu.c")
target_link_libraries (qsfp_management_menu vfio_access)