###########################################################################
# OpenImageIO


# If 'OPENIMAGEHOME' not set, use the env variable of that name if available
if (NOT OPENIMAGEIOHOME)
    if (NOT $ENV{OPENIMAGEIOHOME} STREQUAL "")
        set (OPENIMAGEIOHOME $ENV{OPENIMAGEIOHOME})
    endif ()
endif ()


MESSAGE ( STATUS "OPENIMAGEIOHOME = ${OPENIMAGEIOHOME}" )

find_library ( OPENIMAGEIO_LIBRARY
               NAMES OpenImageIO
               PATHS ${OPENIMAGEIOHOME}/lib )
find_path ( OPENIMAGEIO_INCLUDES OpenImageIO/imageio.h
            ${OPENIMAGEIOHOME}/include )
IF (OPENIMAGEIO_INCLUDES AND OPENIMAGEIO_LIBRARY )
    SET ( OPENIMAGEIO_FOUND TRUE )
    MESSAGE ( STATUS "OpenImageIO includes = ${OPENIMAGEIO_INCLUDES}" )
    MESSAGE ( STATUS "OpenImageIO library = ${OPENIMAGEIO_LIBRARY}" )
    MESSAGE ( STATUS "OpenImageIO namespace = ${OPENIMAGEIO_NAMESPACE}" )
    if (OPENIMAGEIO_NAMESPACE)
        add_definitions ("-DOPENIMAGEIO_NAMESPACE=${OPENIMAGEIO_NAMESPACE}")
    endif ()
ELSE ()
    MESSAGE ( STATUS "OpenImageIO not found" )
ENDIF ()

# end OpenImageIO setup
###########################################################################
