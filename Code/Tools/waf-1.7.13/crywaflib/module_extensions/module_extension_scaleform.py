# Copyright 2001-2019 Crytek GmbH / Crytek Group. All rights reserved.

import os
from waflib import Logs, Utils
from waflib.TaskGen import feature
from waflib.CryModuleExtension import module_extension

@Utils.memoize
def check_path_exists(path):
	if os.path.isdir(path):
		return True
	else:
		Logs.warn('[WARNING] ' + path + ' not found, this feature is excluded from this build.')
	return False