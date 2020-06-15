# cache-dash-h

_cache the help text for your dog-slow python scripts_

---

`cache-dash-h` is a wrapper command that you can use to make CLIs that are slow to startup
more pleasant to use interactively, if you execute `-h/--help` frequently.

When you wrap `cache-dash-h` around a command, it runs your command and caches the stdout
so that subsequent calls can be fast. And of course you don't call the script with `-h` or
`--help`, it'll just exec your script and you won't even realize it was there.

But what if you change the script, or edit one of the files that the script loads during its startup?
Well, `cache-dash-h` tries pretty hard not to serve stale results. If you change the script or any of
the libraries the script loads, it'll notice and rerun your slow script (or at least it _should_ notice).
It does this by recording the hash of every file file your program opens, in addition to the help text.
If any of those files have changed, then it assumes its cached help text is invalid.

## Usage

Let's say you have program that's really slow to start up like this:

```
$ cat ./my-slow-program
#!/usr/bin/env python
import scipy       # slow
import tensorflow  # really slow
import argparse

p = argparse.ArgumentParser(...)
...
args = p.parse_args()
```

And it takes _forever_ just to read the help text
```
$ ./my-slow-program --help
# wait forever...
```

Change your shebang from `#!/usr/bin/env python` to `#!/usr/bin/env cach-dash-h python`! The first
execution will still be slow, but any subsequent executions will be a lot faster.

```
$ cat ./my-fast-program
#!/usr/bin/env cache-dash-h python
import scipy       # slow
import tensorflow  # really slow
import argparse

p = argparse.ArgumentParser(...)
...
args = p.parse_args()
```

```
$ ./my-fast-program --help
# now it's fast (at least the second time)
```


## Compilation

    cd build
    cmake ..
    make -j4
    make test

