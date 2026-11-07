"""TFN OS SDK: the store setup of the tools (the URL of the manager and the signer of the store key).

The tools that talk to a manager (mk_app.py --publish, store_data.py, store_desc.py, store_class.py) and the tool that
signs a package (mk_app.py) read the setup here:
  manager_url()   the default URL of the manager: the environment variable TFN_MANAGER, else the site module, else ''
                  (then the tool needs the URL as an option)
  sign(manifest)  the ECDSA P-256 signature of the store key over the manifest (r || s, 64 bytes), from the site
                  module. With no site module, give mk_app.py --sign-key <a P-256 PEM file> or --nosign.
The site module is an optional file store_site.py next to this file. A store that keeps its key on another computer
puts its own manager_url() and sign(manifest_bytes) there. The public tree has no site module.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def site():
    """The site module, or None."""
    if not os.path.exists(os.path.join(HERE, 'store_site.py')):
        return None
    if HERE not in sys.path:
        sys.path.insert(0, HERE)
    import store_site  # noqa: E402
    return store_site


def manager_url():
    env = os.environ.get('TFN_MANAGER', '').strip()
    if env:
        return env
    s = site()
    return s.manager_url() if s and hasattr(s, 'manager_url') else ''


def sign(manifest_bytes):
    s = site()
    if not s or not hasattr(s, 'sign'):
        sys.exit('no signer of the store key: give --sign-key <a P-256 PEM file>, or --nosign (a board in the '
                 'developer mode takes a package with no signature)')
    return s.sign(manifest_bytes)
