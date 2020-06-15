set -e -x

CMD="catchsegv cache-dash-h"
function setup {
    export CACHEDASHH_STABLEPATH="/dev:/sys"
    export CACHEDASHH_DB=local.db
    rm -f $CACHEDASHH_DB
}

# check that help text is generated
function test1 {
    setup
    $CMD | grep "usage: cache-dash-h"
    $CMD -h  | grep "usage: cache-dash-h"
}

# running a simple command unchanged with no -h
function test2 {
    setup
    $CMD bash -c "echo hello world" | grep "hello world"
}

# caching with --help is given
function test3 {
    setup
    $CMD -v bash --help

    setup
    $CMD -v bash --help | grep "cache-dash-h: Saved to cache 'local.db'"
    $CMD -v bash --help | grep "cache-dash-h: Read from cache 'local.db'"
}

# caching only based on command name (bash) when -n 1 is given
function test4 {
    setup
    $CMD -v -n 1 bash --help abc | grep "cache-dash-h: Saved to cache 'local.db'"
    $CMD -v -n 1 bash --help 123 | grep "cache-dash-h: Read from cache 'local.db'"
}

# caching based on entire command when -n is not given
function test5 {
    setup
    $CMD -v bash --help abc | grep "cache-dash-h: Saved to cache 'local.db'"
    $CMD -v bash --help 123 | grep "cache-dash-h: Saved to cache 'local.db'"
    $CMD -v bash def --help | grep "cache-dash-h: Saved to cache 'local.db'"
    $CMD -v bash 456 --help | grep "cache-dash-h: Saved to cache 'local.db'"

    $CMD -v bash --help abc | grep "cache-dash-h: Read from cache 'local.db'"
    $CMD -v bash --help 123 | grep "cache-dash-h: Read from cache 'local.db'"
    $CMD -v bash def --help | grep "cache-dash-h: Read from cache 'local.db'"
    $CMD -v bash 456 --help | grep "cache-dash-h: Read from cache 'local.db'"
}

function test6 {
    setup
    rm -f /tmp/script{1,2,3}.sh

    echo "head -c 16 < /dev/zero | tr '\0' '\141'" > /tmp/script1.sh
    echo "head -c 65536 < /dev/zero | tr '\0' '\141'" > /tmp/script2.sh
    echo "head -c 131072 < /dev/zero | tr '\0' '\141'" > /tmp/script3.sh

    $CMD -v bash /tmp/script1.sh -h > /dev/null
    $CMD -v bash /tmp/script2.sh -h > /dev/null
    $CMD -v bash /tmp/script3.sh -h > /dev/null
}

# exit status gets forwarded
function test7 {
    setup

    # when command contains -h
    set +e
    $CMD bash /sdfsdf/sdsdfsdfs/doesnt-exist -h
    result=$?
    set -e
    [ "$result" == 127 ]

    # and when command does not contain -h
    set +e
    $CMD bash /sdfsdf/sdsdfsdfs/doesnt-exist
    result=$?
    set -e
    [ "$result" == 127 ]
}

# test read only
function test8
{
    setup
    touch $CACHEDASHH_DB
    chmod -w $CACHEDASHH_DB
    $CMD -v bash --help

    setup
    $CMD -v bash --help
    chmod -w $CACHEDASHH_DB
    $CMD -v bash --help | grep "cache-dash-h: Read from cache 'local.db'"
}

# test -p cmdline
function test9 {
    setup
    rm -f /tmp/abc123.db
    $CMD -v -p /tmp/abc123.db bash script1 abc -h || true
    $CMD -v -p /tmp/abc123.db bash script2 abc -h || true
    sqlite3 -header /tmp/abc123.db 'select * from cmdline'
}

# test -p cmdline
function test10 {
    setup
    tmpdir=$(mktemp -d)
    touch $tmpdir/script.sh
    $CMD -v -p '$ORIGIN1/file.db' bash $tmpdir/script.sh --help
    sqlite3 -header $tmpdir/file.db 'select * from cmdline'
    rm -rf $tmpdir
}

# test deleting file listed as a dependency
function test11 {
    setup
    tmpdir=$(mktemp -d)
    touch $tmpdir/foo
    pushd $tmpdir
    which python
    $CMD -v python -c $'try:\n open("./foo")\nexcept:\n pass' --help
    $CMD -v python -c $'try:\n open("./foo")\nexcept:\n pass' --help | grep "Read from cache"
    rm -f $tmpdir/foo
    # now that file doesn't exist, it's the same as having changed the dep
    $CMD -v python -c $'try:\n open("./foo")\nexcept:\n pass' --help grep "Saved to cache"
    popd
    rm -rf $tmpdir
}

test1
test2
test3
test4
test5
test6
test7
test8
test9
test10
test11
