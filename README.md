## Yac For drupal7
Yac is a shared and lockless memory user data cache for PHP.
it can be used to replace APC or local memcached.

1. Installation
2. Testing

###1. Installation
####Step 1
Enable the module and make sure the YAC extension is installed properly on
the status page (http://yoursite/admin/reports/status).

if not, try this.

```bash
yum install php-pecl-yac -y
```


####Step 2
Add the following code to your settings.php file:

```bash
/**
 * Add YAC Caching.
 */
$conf['cache_backends'] = array('sites/all/modules/yac/yac_cache.inc');
$conf['cache_default_class'] = 'DrupalYACCache';
```

####Step 3 (OPTIONAL)
Visit your site to see or it's still working!


###2. Testing
To be able to test this module open DRUPAL_ROOT/includes/cache.inc and search
for `variable_get('cache_default_class', 'DrupalDatabaseCache')`. and change
this to DrupalYACCache. This is because the $conf[''] array in settings.php
is not always loaded properly.
