<?php
/**
 * @file
 * This integrates the drupal yac cache backend.
 */

// Array to store statistics about the current page yac calls.
$yac_statistics = array();

/**
 * yac cache implementation.
 *
 * This is Drupal's yac cache implementation. It uses Alternative PHP
 * Cache to store cached data. Each cache bin corresponds to a prefix of
 * the yac variables with the same name.
 */
class DrupalYACCache implements DrupalCacheInterface {
  /**
   * @var string
   */
  protected $bin;

  /**
   * @var string
   */
  protected $prefix;

  /**
   * @var boolean
   */
  protected $drush;

  /**
   * @var array
   */
  protected static $remoteClears = array();

  /**
   * Get prefix for bin using the configuration.
   *
   * @param string $bin
   *
   * @return string
   *   Can be an empty string, if no prefix set.
   */
  protected static function getPrefixSettingForBin($bin) {
    $prefixes = variable_get('cache_prefix', '');

    if (is_string($prefixes)) {
      // Variable can be a string, which then considered as a default behavior.
      return $prefixes;
    }

    if (isset($prefixes[$bin])) {
      if (FALSE !== $prefixes[$bin]) {
        // If entry is set and not FALSE, an explicit prefix is set for the bin.
        return $prefixes[$bin];
      } else {
        // If we have an explicit false, it means no prefix whatever is the
        // default configuration.
        return '';
      }
    } else {
      // Key is not set, we can safely rely on default behavior.
      if (isset($prefixes['default']) && FALSE !== $prefixes['default']) {
        return $prefixes['default'];
      } else {
        // When default is not set or an explicit FALSE, this means no prefix.
        return '';
      }
    }
  }

  function __construct($bin) {
    $this->bin = $bin;
    $this->drush = (drupal_is_cli() && function_exists('drush_log'));

    // First we determine the prefix from a setting.
    $prefix = self::getPrefixSettingForBin($this->bin);

    // If we do not have a configured prefix we use the HTTP_HOST.
    if (empty($prefix) && isset($_SERVER['HTTP_HOST'])) {
      // Provide a fallback for multisite. This is on purpose not inside the
      // getPrefixForBin() function in order to decouple the unified prefix
      // variable logic and custom module related security logic, that is not
      // necessary for all backends.
      $prefix = $_SERVER['HTTP_HOST'] . '::';
    } else {
      $prefix = $prefix . '::';
    }

    // When we are in testing mode we add the test prefix.
    if ($test_prefix = drupal_valid_test_ua()) {
      $prefix = $test_prefix . '::' . $prefix;
    }
    else if (isset($GLOBALS['drupal_test_info'])) {
      $prefix = $GLOBALS['drupal_test_info']['test_run_id'] . '::' . $prefix;
    }

    $this->prefix = $prefix;
  }

  /**
   * Function which retrieves the safe key for the cache bin.
   *
   * @return
   *   The safe yac key.
   */
  private function binKey() {
    return $this->prefix . $this->bin . '::';
  }

  /**
   * Function which retrieves the safe key for the cache cid.
   *
   * @param $cid
   *   The cache id.
   * @return
   *   The safe yac key.
   */
  private function key($cid) {
    return $this->binKey() . $cid;
  }

  function get($cid) {

    $yac = new Yac();
    // Add a get to our statistics.
    $GLOBALS['yac_statistics'][] = array('get', $this->bin, array($cid));

    // Fetch the data.
    $yac_key = strlen($this->key($cid)) > 32 ? md5($this->key($cid)) : $cid;
    $cache = $yac->get($yac_key);
    return $this->prepareItem($cache);
  }

  /**
   * Prepare a cached item.
   *
   * Checks that items are either permanent or did not expire.
   *
   * @param $cache
   *   An item loaded from cache_get() or cache_get_multiple().
   * @return
   *   The item with data unserialized as appropriate or FALSE if there is no
   *   valid item to load.
   */
  protected function prepareItem($cache) {
    if (!isset($cache->data) || empty($cache->data)) {
      return FALSE;
    }

    // If enforcing a minimum cache lifetime, validate that the data is
    // currently valid for this user before we return it by making sure the cache
    // entry was created before the timestamp in the current session's cache
    // timer. The cache variable is loaded into the $user object by _drupal_session_read()
    // in session.inc. If the data is permanent or we're not enforcing a minimum
    // cache lifetime always return the cached data.
    global $user;
    if ($cache->expire != CACHE_PERMANENT && variable_get('cache_lifetime', 0) && (isset($user->cache) && $user->cache > $cache->created)) {
      // This cache data is too old and thus not valid for us, ignore it.
      return FALSE;
    }

    return $cache;
  }

  function getMultiple(&$cids) {
    if (!$cids) {
      return array();
    }

    $yac = new Yac();
    $cache = $fetch = $yac_key = array();
    // We need to search the cache with the proper keys and
    // be able to get the original $cid back.
    foreach ($cids as $cid) {
      $yac_key[] = strlen($this->key($cid)) > 32 ? md5($this->key($cid)) : $cid;
    }

    $fetch = $yac->get($yac_key);

    if (!empty($fetch)) {
      foreach ($fetch as $key => $data) {
        $prepare_data = $this->prepareItem($fetch[$key]);
        if ($prepare_data) {
          $cache[$key] = $prepare_data;
        }
      }
    }

    unset($fetch);

    // Add a get to our statistics.
    $GLOBALS['yac_statistics'][] = array('get', $this->bin, $cids);

    $cids = array_diff($cids, array_keys($cache));

    return $cache;
  }

  function set($cid, $data, $expire = CACHE_PERMANENT, array $headers = NULL) {
    $yac = new Yac();

    // Add set to statistics.
    $GLOBALS['yac_statistics'][] = array('set', $this->bin, $cid);

    // Create new cache object.
    $cache = new stdClass();
    $cache->cid = $cid;
    // yac will serialize any structure we give itself.
    $cache->serialized = 0;
    $cache->created = REQUEST_TIME;
    $cache->expire = $expire;
    $cache->headers = isset($headers) ? $headers : NULL;

    $cache->data = $data;

    // What kind of expiration is being used.
    $yac_key = strlen($this->key($cid)) > 32 ? md5($this->key($cid)) : $cid;

    switch ($expire) {
      case CACHE_PERMANENT:
        $set_result = $yac->set($yac_key, $cache, 31536000 + time());
        break;

      case CACHE_TEMPORARY:
        if (variable_get('cache_lifetime', 0) > 0) {
          $set_result = $yac->set($yac_key, $cache, variable_get('cache_lifetime', 0));
        }
        else {
          $set_result = $yac->set($yac_key, $cache, 31536000 + time());
        }
        break;

      default:
        $set_result = $yac->set($yac_key, $cache, $expire + time());
        break;
    }

  }

  /**
   * Delete CID matching the given prefix.
   *
   * @param string $prefix
   */
  protected function deletePrefix($prefix) {
    $yac = new Yac();
    if (!empty($prefix)) {
      $yac_key = strlen($prefix) > 32 ? md5($prefix) : $prefix;
      $yac->delete($yac_key);
    }
    else {
      $yac->flush();
    }
   }

  /**
   * Flush all cache items in a bin.
   */
  function flush() {
    $this->deletePrefix('');
  }

  function clear($cid = NULL, $wildcard = FALSE) {
    $yac = new Yac();
    if ($this->drush) {
      self::$remoteClears[$this->bin][serialize($cid)] = $wildcard;
      // yac uses separate storage for CLI mode, bounce the clear request back
      // into this method on all server nodes via XMLRPC.
      return;
    }

    // Add a get to our statistics.
    $GLOBALS['yac_statistics'][] = array('clear', $this->bin, $cid, (int)$wildcard);

    $yac->flush();
  }

  function isEmpty() {
    return TRUE;
  }

  public static function remoteFlush() {
    if (!module_exists('yac')) {
      drush_log('You need to enable the yac module for remote cache clearing to work. Run drush pm-enable yac.', 'error');
      return;
    }

    global $base_url;
    if (!empty(self::$remoteClears)) {
      // optimize '*' clears.
      $star = serialize('*');
      foreach (self::$remoteClears as $bin => $clears) {
        if (!empty($clears[$star])) {
          self::$remoteClears[$bin] = array($star => TRUE);
        }
      }
      $args = array(
        'yac_drush_flush' => array(array(
          'clears' => self::$remoteClears,
          'cron_key' => variable_get('cron_key', 'drupal'),
        )),
      );
      $uri = $base_url . '/xmlrpc.php';
      $response = xmlrpc($uri, $args);

      if ($response === FALSE) {
        drush_log('xmlrpc() error: (' . xmlrpc_errno() . ') ' . xmlrpc_error_msg(), 'error');
        if ($base_url == 'http://' . basename(conf_path())) {
          drush_log('The base_url might not be set correctly try using the -l/--uri option for drush.', 'warning');
        }
      }
      elseif (!$response['success']) {
        drush_log('yac could not flush cache(s) because ' . $yac_node . ' returned code ' . $response['message'], 'error');
      }
      else {
        drush_log("yac-Remote $yac_node: {$response['message']}", 'success');
      }
    }
  }
}
