# The Qt layer between the QML pages and the Qt-free StoreService (research R-16): AppListModel and StoreController. It adapts and
# decides nothing. ARM: SDK Qt with a manual moc. HOST: host Qt6 when its Core/Gui/Quick development packages are installed; otherwise
# the bridge, the app and their tests are simply not built.

set(PBSTORE_BRIDGE_SOURCES src/app_list_model.cpp src/store_controller.cpp)
set(PBSTORE_BRIDGE_HEADERS include/app_list_model.h include/store_controller.h)

if(PBSTORE_TARGET STREQUAL "ARM")
    pbstore_moc(PBSTORE_BRIDGE_MOC ${PBSTORE_BRIDGE_HEADERS})
    add_library(pbstore_bridge STATIC ${PBSTORE_BRIDGE_SOURCES} ${PBSTORE_BRIDGE_MOC})
    target_include_directories(pbstore_bridge PUBLIC include)
    target_include_directories(pbstore_bridge SYSTEM PUBLIC ${PBSTORE_QT_INCLUDES})
    target_link_libraries(pbstore_bridge PUBLIC pbstore_store ${PBSTORE_QT_LIBS})
else()
    find_package(Qt6 QUIET COMPONENTS Core Gui Qml Quick)
    if(Qt6Core_FOUND AND Qt6Gui_FOUND AND Qt6Quick_FOUND AND Qt6Qml_FOUND)
        set(CMAKE_AUTOMOC ON)
        add_library(pbstore_bridge STATIC ${PBSTORE_BRIDGE_SOURCES} ${PBSTORE_BRIDGE_HEADERS})
        target_include_directories(pbstore_bridge PUBLIC include)
        target_link_libraries(pbstore_bridge PUBLIC pbstore_store Qt6::Core Qt6::Gui Qt6::Quick)
    else()
        message(STATUS "Qt6 Core/Gui/Qml/Quick not found - the Qt bridge, the app and the bridge tests are not built")
    endif()
endif()
