# Making a release

Astir has no release infrastructure beyond git and GitHub, deliberately. Every
commit is a snapshot anyone can check out by hash, and GitHub will produce a
tarball of any tag on demand, so there is nothing to build and nothing to
upload. A release is a tag, plus notes saying what changed.

## Version numbers

The version lives in exactly one place — `AC_INIT` in `configure.ac`. Nothing
else in the tree hardcodes it.

The last field is even for a release and odd for development, inherited from
Xastir and worth keeping. So `master` normally carries an odd number, you set an
even one to release, and you bump back to the next odd one immediately
afterwards.

> **Known gap.** `scripts/AstirGitStamp.sh` generates `gitstring` into
> `src/core/util/compiledate.c` on every build, and nothing reads it. The GTK4
> About dialog does not show a version, and this front end does not parse `-V`
> — it hands its arguments straight to GApplication. So there is currently **no
> way for a user to report which build they are running**, and the sanity check
> below cannot confirm the binary's version. Worth fixing before a release
> anyone else installs.

## The process

1. **Get `master` ready.** Everything that belongs in the release is merged,
   including documentation. Do not start until it is.

2. **Set the version.** Edit `configure.ac`, commit it on its own:

   ```sh
   git add configure.ac
   git commit          # "Update version number for release X.Y.Z"
   ```

3. **Build it the way a stranger would** — from scratch, in a fresh directory,
   out of tree:

   ```sh
   ./bootstrap.sh                  # or autoreconf -i
   mkdir ../release-check && cd ../release-check
   ../astir/configure
   make
   ```

   Read the configure summary. A release configured without shapelib because the
   machine happened to be missing PCRE is a release that draws no maps.

4. **Run the gates.** Building is not testing. At minimum the GTK4 smoke test
   and, if the harnesses are present on your machine, the label and symbol
   render checks. Say in the release notes what you ran.

5. **Check the tree is clean.**

   ```sh
   git status          # on master, clean, one commit ahead of origin/master
   ```

6. **Tag it, annotated.**

   ```sh
   git tag -a -m "Astir Release X.Y.Z" Release-X.Y.Z
   ```

   The `-a` matters: `git describe` needs an annotated tag to give a useful
   answer, and that is what feeds the build stamp.

7. **Verify the tarball GitHub will produce.** This catches files that exist on
   your machine but were never committed, which is the failure this step is
   for:

   ```sh
   git archive --format=tar.gz --prefix=Astir-Release-X.Y.Z/ Release-X.Y.Z \
       > /tmp/Astir-Release-X.Y.Z.tar.gz
   cd /tmp && tar xzf Astir-Release-X.Y.Z.tar.gz
   cd Astir-Release-X.Y.Z && ./bootstrap.sh && mkdir b && cd b && ../configure && make
   ```

   If it fails, fix it on `master`, commit, then move the tag:

   ```sh
   git tag -d Release-X.Y.Z
   git tag -a -m "Astir Release X.Y.Z" Release-X.Y.Z
   ```

   and repeat. Only move a tag that has not been pushed.

8. **Push.**

   ```sh
   git push origin master
   git push origin Release-X.Y.Z
   ```

9. **Draft the release on GitHub**, at
   <https://github.com/queueingqt/Astir/releases>. Select the existing tag,
   name it "Astir Release X.Y.Z", and write notes covering what changed, what is
   still not implemented, and what was actually tested. Publish.

10. **Bump `master` to the next odd version** and push, so development builds
    are distinguishable from the release:

    ```sh
    git add configure.ac && git commit && git push
    ```

## Release notes

Astir is a work in progress and the notes should read that way. Someone
installing it needs to know, before they spend an evening on it, that it cannot
transmit and has no settings window. Lead with the honest limitations, then the
changes. A release note that oversells costs more goodwill than a missing
feature does.

There are no mailing lists for this fork. Xastir's lists belong to Xastir and
are not the place to announce Astir releases; use GitHub Releases and Issues.

## Reporting bugs

<https://github.com/queueingqt/Astir/issues>, which is also the address in
`AC_INIT`, so `configure --help` points at the right place.
