# bash completion for wayweather

_wayweather_locations() {
  wayweather list --json 2>/dev/null | sed -n 's/^[[:space:]]*"id":[[:space:]]*\([0-9]*\).*/\1/p'
}

_wayweather() {
  local cur prev cmd i
  COMPREPLY=()
  cur=${COMP_WORDS[COMP_CWORD]}
  prev=${COMP_WORDS[COMP_CWORD-1]}

  local commands="get print daemon tui search add list remove edit use default
    next prev set units geo refresh reset config help version"
  local unit_opts="--units --temp --wind --precip --pressure --clock"

  cmd=""
  for ((i = 1; i < COMP_CWORD; i++)); do
    case ${COMP_WORDS[i]} in
      -c|--config) ((i++)) ;;
      -*) ;;
      *) cmd=${COMP_WORDS[i]}; break ;;
    esac
  done

  case $prev in
    -c|--config) COMPREPLY=($(compgen -f -- "$cur")); return ;;
    -u|--units) COMPREPLY=($(compgen -W "metric imperial" -- "$cur")); return ;;
    --temp|--temperature) COMPREPLY=($(compgen -W "celsius fahrenheit" -- "$cur")); return ;;
    --wind) COMPREPLY=($(compgen -W "kmh ms mph kn" -- "$cur")); return ;;
    --precip|--precipitation) COMPREPLY=($(compgen -W "mm inch" -- "$cur")); return ;;
    --pressure) COMPREPLY=($(compgen -W "hpa inhg mmhg kpa" -- "$cur")); return ;;
    --clock) COMPREPLY=($(compgen -W "24h 12h" -- "$cur")); return ;;
    -l|--location) COMPREPLY=($(compgen -W "$(_wayweather_locations) default" -- "$cur")); return ;;
  esac

  if [[ -z $cmd ]]; then
    COMPREPLY=($(compgen -W "$commands --config --verbose --no-color --help --version" -- "$cur"))
    return
  fi

  case $cmd in
    get|print) COMPREPLY=($(compgen -W "--no-icon --location --force" -- "$cur")) ;;
    daemon) COMPREPLY=($(compgen -W "--interval --no-icon --location" -- "$cur")) ;;
    search) COMPREPLY=($(compgen -W "--count" -- "$cur")) ;;
    add) COMPREPLY=($(compgen -W "--pick --lat --lon --city --region --country --name --units --default --use" -- "$cur")) ;;
    list) COMPREPLY=($(compgen -W "--json" -- "$cur")) ;;
    remove) COMPREPLY=($(compgen -W "$(_wayweather_locations) --yes" -- "$cur")) ;;
    edit) COMPREPLY=($(compgen -W "$(_wayweather_locations) --name --city --region --country --lat --lon $unit_opts" -- "$cur")) ;;
    use) COMPREPLY=($(compgen -W "$(_wayweather_locations) default geo" -- "$cur")) ;;
    default) COMPREPLY=($(compgen -W "$(_wayweather_locations) --clear" -- "$cur")) ;;
    set) COMPREPLY=($(compgen -W "--lat --lon --city --region --country --name --units --save --skip-config" -- "$cur")) ;;
    units) COMPREPLY=($(compgen -W "metric imperial $unit_opts --location" -- "$cur")) ;;
    geo)
      if [[ $prev == provider ]]; then
        COMPREPLY=($(compgen -W "auto gpsd geoclue ip" -- "$cur"))
      else
        COMPREPLY=($(compgen -W "on off toggle locate save provider" -- "$cur"))
      fi ;;
    reset) COMPREPLY=($(compgen -W "--units --all --yes" -- "$cur")) ;;
    config) COMPREPLY=($(compgen -W "path show edit check" -- "$cur")) ;;
    help) COMPREPLY=($(compgen -W "$commands format" -- "$cur")) ;;
  esac
}

complete -F _wayweather wayweather
