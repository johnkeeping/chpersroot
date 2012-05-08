#!/bin/sh

cd "$(dirname "$0")"

n_passed=0
n_failed=0

_run_success_test() {
	echo "$2" >"test.$$.ini" &&
	echo "$5" >"test.$$.expected" &&
	./initest "test.$$.ini" "$3" "$4" >"test.$$.actual" &&
	diff -u "test.$$.expected" "test.$$.actual" &&
	rm -f "test.$$."*
}

test_expect_success() {
	if _run_success_test "$@"
	then
		echo "test passed: $1"
		n_passed=$((n_passed + 1))
	else
		echo "test failed: $1"
		n_failed=$((n_failed + 1))
	fi
}

test_expect_success 'simple ini file' '
[gentoo32]
	rootdir = foo bar' gentoo32 rootdir 'foo bar'

test_expect_success 'initial comment' '
; This is a comment!
[gentoo32]
	rootdir = foo bar' gentoo32 rootdir 'foo bar'

test_expect_success 'comment after section' '
[gentoo32] ; comment
	rootdir = foo bar' gentoo32 rootdir 'foo bar'

test_expect_success 'comment after value' '
[gentoo32]
	rootdir = foo bar ; comment' gentoo32 rootdir 'foo bar'

test_expect_success 'comment on line' '
[gentoo32]
	; This is a comment
	rootdir = foo bar' gentoo32 rootdir 'foo bar'

test_expect_success 'sq value' "
[gentoo32]
	rootdir = 'foo bar ; comment'" gentoo32 rootdir 'foo bar ; comment'

test_expect_success 'dq value' '
[gentoo32]
	rootdir = "foo bar ; comment"' gentoo32 rootdir 'foo bar ; comment'

test_expect_success 'continued value' '
[gentoo32]
	rootdir = foo \
bar' gentoo32 rootdir 'foo bar'

test_expect_success 'second section' '
[gentoo32]
	rootdir = "foo bar"
; Comment preceding section 2
[section two]
	rootdir = /jail' 'section two' rootdir '/jail'


printf '%d/%d passed\n' $n_passed $((n_passed + n_failed))
test $n_failed = 0
