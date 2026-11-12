###########################################
#                                         #
#  This file is released under GPL >= 2   #
#                                         #
###########################################


# required stuff
find_package( TQt )
find_package( TDE )

tde_setup_compiler_flags( )
tde_setup_largefiles( )


##### check for gcc visibility support

if( WITH_GCC_VISIBILITY )
  tde_setup_gcc_visibility( )
endif( WITH_GCC_VISIBILITY )


##### native PipeWire backend

if( WITH_PIPEWIRE )
  pkg_search_module( PIPEWIRE libpipewire-0.3 )
  if( NOT PIPEWIRE_FOUND )
    tde_message_fatal( "libpipewire-0.3 is required for the PipeWire backend (or configure with -DWITH_PIPEWIRE=OFF)" )
  endif( )
  set( HAVE_PIPEWIRE 1 )
endif( )


##### PulseAudio backend (also works with pipewire-pulse)

if( WITH_PULSE )
  pkg_search_module( PULSE libpulse )
  if( NOT PULSE_FOUND )
    tde_message_fatal( "libpulse is required for the PulseAudio backend (or configure with -DWITH_PULSE=OFF)" )
  endif( )
  set( HAVE_PULSE 1 )
endif( )

if( NOT HAVE_PIPEWIRE AND NOT HAVE_PULSE )
  tde_message_fatal( "At least one audio backend (WITH_PIPEWIRE or WITH_PULSE) must be enabled" )
endif( )
