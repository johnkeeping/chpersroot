# Bash completion script for chpersroot.
#
# To use this file add something like the following to your .bashrc:
#
#   . /path/to/chpersroot-completion.bash
#   _complete -F _chpersroot my_chpersroot1
#
# where my_chpersroot1 is the name by which you execute chpersroot.
#
# Note that this file requires certain functions from the bash_completion
# script from the bash-completion project[1] so you will need that set up
# before using this script.
#
# [1] http://bash-completion.alioth.debian.org/


# Parses out the rootdir specification for a command from the user's
# configuration file.
#
# @param $1  The command name to use for finding the correct section of the
#            configuration file.
#
__chpersroot_newroot() {
    local cmd CHPERSROOT_CONFIG
    cmd=$1
    : ${CHPERSROOT_CONFIG:=@@CONFIG_PATH@@}

    sed -n -e "/\\[$cmd\\]/,/^\\[/ {
        /^[ 	]*rootdir[ 	]*[:=]/ {
            s/^[ 	]*rootdir[ 	]*[:=][ 	]*//
            p
            q
        }
    }" "$CHPERSROOT_CONFIG" 2>/dev/null
}


# Finds the home directory for the specified user within the chroot.
#
# @param $1  The UID of the target user.
#
__chpersroot_homedir() {
    local userid IFS name password uid gid gecos directory shell
    userid=$1
    IFS=:
    while read -r name password uid gid gecos directory shell; do
        if [ "$uid" = "$userid" ]; then
            echo "$directory"
            break
        fi
    done <"$newroot/etc/passwd"
}


# Prepends $newroot to each element of a path.
#
# @param $1  The path to process.
#
__chpersroot_process_path() {
    local IFS=':'
    set -- $1
    while test $# != 0; do
        printf "%s" "$newroot$1"
        shift
        test $# = 0 || printf :
    done
}


# Filter for processing file/directory names returned by compgen and removing
# a prefix from all paths.  Additionally a trailing slash is added to
# directory names which do not already have one.
#
# @param $1  The prefix to remove from all lines.
#
__chpersroot_filedir() {
    local dir prefix
    prefix=$1
    while read -r dir; do
        [ -d "$dir" -a "${dir%/}" = "$dir" ] && dir=$dir/
        echo "${dir##$prefix}"
    done
}


# Filter for processing file/directory names returned by "compgen -f" and
# removing any entries that are neither a directory nor executable.  This
# filter also performs the same processing as __chpersroot_filedir.
#
# @param $1  The prefix to pass to __chpersroot_filedir
#
__chpersroot_execfiles() {
    local f prefix
    prefix=$1
    while read -r f; do
        [ -x "$f" -o -d "$f" ] && echo "$f"
    done | __chpersroot_filedir "$prefix"
}


# Override for compgen which prepends the new root path to words which will be
# completed as files or directories and uses __chpersroot_filedir to filter
# the output in this case.
#
__chpersroot_compgen() {
    local filter= args=() arg prefix uid
    while test $# != 0; do
        arg=$1
        shift
        if test $# = 0 -a -n "$filter"; then
            if [ "${arg#/}" = "$arg" ]; then
                if _complete_as_root; then
                    uid=0
                else
                    uid=$EUID
                fi
                prefix="$newroot$(__chpersroot_homedir $uid)/"
            else
                prefix=$newroot
            fi
            arg=$prefix$arg
        fi
        case "$arg" in
            --chpersroot-exec)
                arg=-f
                filter=__chpersroot_execfiles
                ;;
            -d|-f)
                filter=__chpersroot_filedir
                ;;
        esac
        args+=( "$arg" )
    done
    if [ -n "$filter" ]; then
        builtin compgen "${args[@]}" | $filter "$prefix"
    else
        builtin compgen "${args[@]}"
    fi
}


__chpersroot_compopts=

# Sets or clears a flag in __chpersroot_compopts.
#
# @param $1  Either -o or +o, with the same meaning as for compgen.
# @param $2  The flag to be set or cleared.
#
__chpersroot_compopt_set() {
    local flag=$1 opt=$2

    case "$flag" in
    +o)
        __chpersroot_compopts=$(echo " $__chpersroot_compopts " |
                                sed "s/ $opt //")
        ;;
    -o)
        __chpersroot_compopts="$__chpersroot_compopts $opt"
        ;;
    esac
}


# Override for compopt which sets the dirnames and plusdirs options in
# __chpersroot_compopts instead of passing them to builtin compopt.  All
# other arguments are passed to builtin compopt.
#
# As an additional feature, if the first argument is "compopt", builtin
# compopt will not be called, allowing this function to be passed the output
# of "compopt <command>".
#
__chpersroot_compopt() {
    local args=() arg opt
    while test $# != 0; do
        arg=$1
        case "$arg" in
        +o|-o)
            shift
            opt=$1
            case "$opt" in
            dirnames|plusdirs|nospace)
                __chpersroot_compopt_set $arg "$opt"
                ;;
            *)
                args+=( "$arg" "$opt" )
                ;;
            esac
            ;;
        *)
            args+=( "$arg" )
            ;;
        esac
        shift
    done
    test ${#args} = 0 -o "${args[0]}" = compopt ||
    builtin compopt "${args[@]}"
}


# Applies options set in __chpersroot_compopts to COMPREPLY.
#
__chpersroot_process_compopts() {
    local arg
    case " $__chpersroot_compopts " in
    *' plusdirs '*)
        arg=-d
        ;;
    *' dirnames '*)
        test ${#COMPREPLY} = 0 || return 0
        arg=-d
        ;;
    *' default '*)
        test ${#COMPREPLY} = 0 || return 0
        arg=-f
        ;;
    *)
        return 0
        ;;
    esac
    local cur
    _get_comp_words_by_ref cur
    COMPREPLY=( "${COMPREPLY[@]}" $(compgen $arg -- "$cur") )
}


# Completes a command using the path inside the chroot.
#
__chpersroot_complete_path_command() {
    if _complete_as_root; then
        newpath=$(__chpersroot_process_path "@@ENV_SUPATH@@")
    else
        newpath=$(__chpersroot_process_path "@@ENV_PATH@@")
    fi

    local PATH=$newpath
    COMPREPLY=( $(compgen -c -- "$cur") )
}


# Completes a command inside the chroot.
#
__chpersroot_complete_command() {
    local newpath cur
    _get_comp_words_by_ref cur

    case "$cur" in
    */*)
        builtin compopt +o filenames +o default -o nospace
        COMPREPLY=( $(__chpersroot_compgen --chpersroot-exec "$cur") )
        if [ ${#COMPREPLY[@]} = 1 ]; then
            # If there is only a single completion option and it does not end
            # with a slash, append a space to it, since we instruct readline
            # not to do this.
            [ "${COMPREPLY[0]%/}" = "${COMPREPLY[0]}" ] && COMPREPLY[0]+=' '
        fi
        ;;
    *)
        __chpersroot_complete_path_command
        ;;
    esac
}


# Completion for when the current word is an argument to a function within
# the chroot.
#
# @param $1  The offset in COMP_WORDS to the command within the chroot.
#
__chpersroot_complete_args() {
    local cmd offset cur
    local -f compgen compopt
    offset=$1
    shift
    cmd=${COMP_WORDS[offset]}

    _get_comp_words_by_ref cur
    compopt -o nospace +o filenames
    compgen() {
        __chpersroot_compgen "$@"
    }
    compopt() {
        __chpersroot_compopt "$@"
    }
    __chpersroot_compopts=
    if complete -p "${cmd##*/}" &>/dev/null; then
        __chpersroot_compopt $(builtin compopt "${cmd##*/}")
        _command "$@"
    else
        builtin compopt -o nospace
        COMPREPLY=( $(compgen -f -- "$cur") )
    fi
    builtin compopt +o filenames +o default
    __chpersroot_process_compopts
    if [ ${#COMPREPLY[@]} = 1 ]; then
        case " $__chpersroot_compopts " in
            *' nospace '*)
                ;;
            *)
                [ "${COMPREPLY[0]%/}" = "${COMPREPLY[0]}" ] &&
                builtin compopt +o nospace
                ;;
        esac
    fi
    unset compgen compopt
}


# Completion function for chpersroot.
#
_chpersroot() {
    local newroot offset

    newroot=$(__chpersroot_newroot "$1")
    [[ -z "$newroot" ]] && return 1

    if test $COMP_CWORD = 1; then
        __chpersroot_complete_command
        return
    fi

    # Hardcode offset as 1 for now since chpersroot doesn't process any
    # arguments at the moment.
    offset=1
    __chpersroot_complete_args $offset "$@"
}
