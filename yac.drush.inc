<?php


/**
 * Implements hook_drush_exit().
 */
function yac_drush_exit() {
  if (class_exists('DrupalYACCache')){
    DrupalYACCache::remoteFlush();
  }
}
