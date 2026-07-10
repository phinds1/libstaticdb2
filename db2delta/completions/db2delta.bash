# Bash completion for db2delta
# Source this file or install to /etc/bash_completion.d/db2delta
# Install: sudo cp completions/db2delta.bash /etc/bash_completion.d/db2delta

_db2delta() {
    local cur prev words cword
    _init_completion 2>/dev/null || {
        COMPREPLY=()
        cur="${COMP_WORDS[COMP_CWORD]}"
        prev="${COMP_WORDS[COMP_CWORD-1]}"
        words=("${COMP_WORDS[@]}")
        cword=$COMP_CWORD
    }

    local subcommands="version exec apply help"
    local conn_opts="-d --database -u --user -p --password"

    # Flags that consume the next word (so we skip their value in subcommand detection)
    local value_flags="-d --database -u --user -p --password"

    # Handle value completion for flags that take an argument
    case "$prev" in
        -d|--database|-p|--password)
            # DSN and password: no automatic completion
            return ;;
        -u|--user)
            COMPREPLY=($(compgen -u -- "$cur"))
            return ;;
    esac

    # Find the subcommand by scanning words, skipping flag values
    local subcmd=""
    local i=1
    while [[ $i -lt ${#words[@]} ]]; do
        local word="${words[$i]}"
        # If this word is a value-consuming flag, skip it and its value
        if [[ " $value_flags " == *" $word "* ]]; then
            ((i+=2))
            continue
        fi
        # Skip boolean flags
        if [[ "$word" == -* ]]; then
            ((i+=1))
            continue
        fi
        # First non-flag word is the subcommand
        if [[ $i -gt 0 ]]; then
            subcmd="$word"
            break
        fi
        ((i+=1))
    done

    # No subcommand yet — complete subcommands or pre-subcommand flags
    if [[ -z "$subcmd" ]]; then
        if [[ "$cur" == -* ]]; then
            COMPREPLY=($(compgen -W "$conn_opts -c --continue-on-error -x --script" -- "$cur"))
        else
            COMPREPLY=($(compgen -W "$subcommands" -- "$cur"))
        fi
        return
    fi

    case "$subcmd" in
        version|help)
            # No further arguments
            ;;

        exec)
            if [[ "$cur" == -* ]]; then
                COMPREPLY=($(compgen -W "$conn_opts -x --script" -- "$cur"))
            fi
            # SQL string is free-form — no file completion
            ;;

        apply)
            if [[ "$cur" == -* ]]; then
                COMPREPLY=($(compgen -W "$conn_opts -c --continue-on-error -x --script" -- "$cur"))
            else
                # Complete .delta files and directories
                local IFS=$'\n'
                COMPREPLY=($(compgen -f -X '!*.delta' -- "$cur"))
                COMPREPLY+=($(compgen -d -- "$cur"))
            fi
            ;;
    esac
}

complete -F _db2delta db2delta
