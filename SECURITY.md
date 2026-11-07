# Security policy

## Report a security fault in private

Do not report a security fault in a public issue, a pull request or a discussion. Other persons can read them before
there is a fix.

Use the private vulnerability reporting of GitHub for the repo
[omeriko9/tfn-os](https://github.com/omeriko9/tfn-os):

1. Open the **Security** tab of the repo, or go to https://github.com/omeriko9/tfn-os/security/advisories/new.
2. Push **Report a vulnerability**.
3. Fill in the form.

Only the maintainer and you can see the report. This is the only channel for a security report: the project has no
e-mail address for it. You need a GitHub account.

Give this data in your report:

- the part of TFN OS (for example the app loader, the signature check, the web server of the board, the update over
  the air, the SDK tools);
- the board kind and the firmware version (the console command `ota` prints the version);
- the steps that show the fault, and what an attacker can do with it;
- a proof of concept, if you have one;
- your name for the credit, or "no credit".

## What happens after your report

1. The maintainer confirms that the report came in.
2. The maintainer checks the fault and tells you the result.
3. The maintainer makes a fix and a release, and then publishes a security advisory. The advisory gives you the
   credit, if you want it.

Do not publish the fault before the fix is in a release, unless you agree a date with the maintainer.

## The supported versions

Only the newest release gets security fixes. Update your board to the newest release before you report a fault, and
check that the fault is still there.

| Version | Security fixes |
|---|---|
| The newest release | Yes |
| All older releases | No |
| A DEV build of a branch | No |

## The trust model (what is not a fault of TFN OS)

- **An app runs native code.** An app file is machine code for the ESP32 chip. The permission classes of the store
  tell the user what an app asks for, and the loader links only the names of those classes. They are not a sandbox:
  an app can read all of the memory of the board. Install only the apps that you trust. An app that can harm a board
  after you install it is not a fault of TFN OS. A fault in the signature check or in the permission check of the
  loader is a fault: report it.
- **A person with physical access.** TFN OS does not use secure boot or flash encryption (they are permanent
  changes of the chip). A person with the board and a USB cable can read and write its flash. That is not a fault.
- **Test only on your own boards.** Do not test on a board or a network of another person without the permission of
  its owner.
