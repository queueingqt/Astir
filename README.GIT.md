# Working on Astir with git

This document covers what is specific to this project. It does not teach git —
there are far better resources for that, listed at the end, and duplicating them
here would only produce a worse copy that goes stale.

## Getting the source

```sh
git clone https://github.com/queueingqt/Astir.git
cd Astir
```

This is a fork of [Xastir](https://github.com/Xastir/Xastir) and is a separate
program, not a newer version of it. See [FORK.md](FORK.md).

Then build it — [INSTALL.md](INSTALL.md) has the detail:

```sh
./bootstrap.sh
./configure
make
sudo make install
```

## Keeping up to date

```sh
git pull
./bootstrap.sh          # only needed when configure.ac or a Makefile.am changed
./configure
make
sudo make install
```

`bootstrap.sh` needs autoconf 2.53 or newer and automake 1.16 or newer.
`autoreconf -i` does the same thing and is the more modern spelling; either
works. If bootstrap fails, stop — there will be no `configure` script, and every
subsequent step will fail in a more confusing way. The cause is almost always a
missing autotools package.

## Contributing a change

The flow is fork and pull request:

1. Fork the repository on GitHub.
2. Add your fork as a second remote, alongside `origin`:
   ```sh
   git remote add myfork git@github.com:yourname/Astir.git
   ```
3. Branch from `master` for each piece of work:
   ```sh
   git checkout master && git pull
   git checkout -b short-description-of-the-change
   ```
4. Commit as you go. Small, self-contained commits that each build and run beat
   one enormous commit, both for review and for the person bisecting a
   regression two years from now.
5. Push the branch to your fork and open a pull request:
   ```sh
   git push myfork short-description-of-the-change
   ```

Open the pull request as a draft with `WIP:` in the title if you want early
review of something unfinished. Nobody can merge a draft, which is the point.

## Commit messages

The first line is a summary of 50 characters or so, then a blank line, then the
body. `git_commit_message_template` in the tree is a starting point:

```sh
git config commit.template git_commit_message_template
```

Beyond the formatting, two things matter more here than they do on most
projects:

**Say why, not what.** The diff already says what changed. What it cannot say is
what was wrong before, what else you tried, or which of two defensible
approaches you picked and on what grounds. That is the part that is expensive to
reconstruct and cheap to write down now.

**Say what you verified, and what you did not.** This code base has a long
history of changes that compiled, linked, looked right and were wrong, and of
test harnesses that passed because they were measuring nothing. If you ran the
smoke test, say so. If you could not test a configuration because the library
is not installed on your machine, say that too — an unverified change that
admits it is useful, and one that implies otherwise costs somebody a day.

Do not attribute commits to tools. Authorship is the person who decided the
change was correct.

## Branches and worktrees

Astir's default branch is `master`. To have two branches checked out at once
without a second clone:

```sh
git worktree add ../astir-other-branch branchname
```

That is genuinely useful here for A/B comparisons — build the old commit in a
worktree and run both binaries against the same fixture. Check that the
worktree's `config.h` matches this tree's before believing any comparison; a
differently configured build renders differently for reasons that have nothing
to do with your change.

## Attribution with more than one GitHub account

GitHub attributes commits by the email in your git config. If commits are
landing under the wrong account, set a local identity in this repository, which
overrides the global one:

```sh
git config user.name "Your Name"
git config user.email "you@example.com"
git config --local -l          # confirm
```

If they still come out wrong, check that `GIT_AUTHOR_EMAIL` and
`GIT_COMMITTER_EMAIL` are not set in your environment.

## Learning git properly

* [Pro Git](https://git-scm.com/book/en/v2) — the book, free, and the right
  place to start.
* [How to write a git commit message](https://chris.beams.io/posts/git-commit/)
* [Think Like (a) Git](https://think-like-a-git.net/) — for when branching and
  merging still feel arbitrary.
* [Changing history](https://justinhileman.info/article/changing-history/) —
  rebase, amend, and how to undo them.
