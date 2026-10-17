# fish completion for wayweather

function __wayweather_locations
    wayweather list --json 2>/dev/null | string match -rg '"id":\s*(\d+)'
    echo default
end

set -l cmds get print daemon tui search add list remove edit use default next prev set units geo refresh reset config help version

complete -c wayweather -f
complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -s c -l config -r -F -d 'Config file'
complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -l verbose -d 'Log progress'
complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -l no-color -d 'Disable colors'
complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -s h -l help -d 'Show help'
complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -s v -l version -d 'Show version'

complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -a get -d 'Print Waybar JSON once'
complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -a print -d 'Detailed terminal report'
complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -a daemon -d 'Stream Waybar JSON'
complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -a tui -d 'Interactive manager'
complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -a search -d 'Search places'
complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -a add -d 'Save a location'
complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -a list -d 'List saved locations'
complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -a remove -d 'Delete locations'
complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -a edit -d 'Change a location'
complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -a use -d 'Show a location'
complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -a default -d 'Default location'
complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -a next -d 'Next location'
complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -a prev -d 'Previous location'
complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -a set -d 'Set current location'
complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -a units -d 'Measurements'
complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -a geo -d 'Geolocation'
complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -a refresh -d 'Refetch now'
complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -a reset -d 'Reset current location'
complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -a config -d 'Config file'
complete -c wayweather -n "not __fish_seen_subcommand_from $cmds" -a help -d 'Show help'

complete -c wayweather -n "__fish_seen_subcommand_from get print daemon" -l no-icon -d 'No Nerd Font icons'
complete -c wayweather -n "__fish_seen_subcommand_from get print daemon units" -s l -l location -x -a '(__wayweather_locations)'
complete -c wayweather -n "__fish_seen_subcommand_from get print" -s f -l force -d 'Bypass caches'
complete -c wayweather -n "__fish_seen_subcommand_from daemon" -s i -l interval -x -d 'Seconds'
complete -c wayweather -n "__fish_seen_subcommand_from remove edit use default" -a '(__wayweather_locations)'
complete -c wayweather -n "__fish_seen_subcommand_from use" -a geo -d 'Geolocation'
complete -c wayweather -n "__fish_seen_subcommand_from remove" -s y -l yes
complete -c wayweather -n "__fish_seen_subcommand_from add" -s p -l pick -x
complete -c wayweather -n "__fish_seen_subcommand_from add" -s d -l default
complete -c wayweather -n "__fish_seen_subcommand_from add" -l use
complete -c wayweather -n "__fish_seen_subcommand_from add edit set" -l lat -x
complete -c wayweather -n "__fish_seen_subcommand_from add edit set" -l lon -x
complete -c wayweather -n "__fish_seen_subcommand_from add edit set" -l city -x
complete -c wayweather -n "__fish_seen_subcommand_from add edit set" -l region -x
complete -c wayweather -n "__fish_seen_subcommand_from add edit set" -l country -x
complete -c wayweather -n "__fish_seen_subcommand_from add edit set" -s N -l name -x
complete -c wayweather -n "__fish_seen_subcommand_from add edit set units" -s u -l units -x -a 'metric imperial'
complete -c wayweather -n "__fish_seen_subcommand_from edit units" -l temp -x -a 'celsius fahrenheit'
complete -c wayweather -n "__fish_seen_subcommand_from edit units" -l wind -x -a 'kmh ms mph kn'
complete -c wayweather -n "__fish_seen_subcommand_from edit units" -l precip -x -a 'mm inch'
complete -c wayweather -n "__fish_seen_subcommand_from edit units" -l pressure -x -a 'hpa inhg mmhg kpa'
complete -c wayweather -n "__fish_seen_subcommand_from edit units" -l clock -x -a '24h 12h'
complete -c wayweather -n "__fish_seen_subcommand_from units" -a 'metric imperial'
complete -c wayweather -n "__fish_seen_subcommand_from set" -s s -l save
complete -c wayweather -n "__fish_seen_subcommand_from list" -l json
complete -c wayweather -n "__fish_seen_subcommand_from geo; and not __fish_seen_subcommand_from provider" -a 'on off toggle locate save provider'
complete -c wayweather -n "__fish_seen_subcommand_from provider" -a 'auto gpsd geoclue ip'
complete -c wayweather -n "__fish_seen_subcommand_from reset" -s u -l units
complete -c wayweather -n "__fish_seen_subcommand_from reset" -l all
complete -c wayweather -n "__fish_seen_subcommand_from reset" -s y -l yes
complete -c wayweather -n "__fish_seen_subcommand_from config" -a 'path show edit check'
complete -c wayweather -n "__fish_seen_subcommand_from help" -a "$cmds format"
