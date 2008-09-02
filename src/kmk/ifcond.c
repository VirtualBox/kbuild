#ifdef CONFIG_WITH_IF_CONDITIONALS

#include "make.h"
#include <assert.h>

#include <glob.h>

#include "dep.h"
#include "filedef.h"
#include "job.h"
#include "commands.h"
#include "variable.h"
#include "rule.h"
#include "debug.h"
#include "hash.h"




int ifcond_eval(char *line, const struct floc *flocp)
{
    error (flocp, _("if conditionals are not implemented yet"));
    
    return -1;
}


#endif /* CONFIG_WITH_IF_CONDITIONALS */
