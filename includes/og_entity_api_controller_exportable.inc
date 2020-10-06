<?php

/**
 * @file
 * Provides a controller building upon the core controller but providing more
 * features like full CRUD functionality.
 */


/**
 * A controller implementing exportables stored in the database.
 */
class OgEntityAPIControllerExportable extends OgEntityAPIController {

  protected $entityCacheByName = array();
  protected $nameKey, $statusKey, $moduleKey;

  /**
   * Overridden.
   *
   * Allows specifying a name key serving as uniform identifier for this entity
   * type while still internally we are using numeric identifieres.
   */
  public function __construct($entityType) {
    parent::__construct($entityType);
    // Use the name key as primary identifier.
    $this->nameKey = isset($this->entityInfo['entity keys']['name']) ? $this->entityInfo['entity keys']['name'] : $this->idKey;
    if (!empty($this->entityInfo['exportable'])) {
      $this->statusKey = isset($this->entityInfo['entity keys']['status']) ? $this->entityInfo['entity keys']['status'] : 'status';
      $this->moduleKey = isset($this->entityInfo['entity keys']['module']) ? $this->entityInfo['entity keys']['module'] : 'module';
    }
  }

  /**
   * Support loading by name key.
   */
  protected function buildQuery($ids, $conditions = array(), $revision_id = FALSE) {
    // Add the id condition ourself, as we might have a separate name key.
    $query = parent::buildQuery(array(), $conditions, $revision_id);
    if ($ids) {
      // Support loading by numeric ids as well as by machine names.
      $key = is_numeric(reset($ids)) ? $this->idKey : $this->nameKey;
      $query->condition("base.$key", $ids, 'IN');
    }
    return $query;
  }

  /**
   * Overridden to support passing numeric ids as well as names as $ids.
   */
  public function load($ids = array(), $conditions = array()) {
    $entities = array();

    // Only do something if loaded by names.
    if (!$ids || $this->nameKey == $this->idKey || is_numeric(reset($ids))) {
      return parent::load($ids, $conditions);
    }

    // Revisions are not statically cached, and require a different query to
    // other conditions, so separate the revision id into its own variable.
    if ($this->revisionKey && isset($conditions[$this->revisionKey])) {
      $revision_id = $conditions[$this->revisionKey];
      unset($conditions[$this->revisionKey]);
    }
    else {
      $revision_id = FALSE;
    }
    $passed_ids = !empty($ids) ? array_flip($ids) : FALSE;

    // Care about the static cache.
    if ($this->cache && !$revision_id) {
      $entities = $this->cacheGetByName($ids, $conditions);
    }
    // If any entities were loaded, remove them from the ids still to load.
    if ($entities) {
      $ids = array_keys(array_diff_key($passed_ids, $entities));
    }

    $entities_by_id = parent::load($ids, $conditions);
    $entities += entity_key_array_by_property($entities_by_id, $this->nameKey);

    // Ensure that the returned array is keyed by numeric id and ordered the
    // same as the original $ids array and remove any invalid ids.
    $return = array();
    foreach ($passed_ids as $name => $value) {
      if (isset($entities[$name])) {
        $return[$entities[$name]->{$this->idKey}] = $entities[$name];
      }
    }
    return $return;
  }

  /**
   * Overridden.
   * @see DrupalDefaultEntityController::cacheGet()
   */
  protected function cacheGet($ids, $conditions = array()) {
    if (!empty($this->entityCache) && $ids !== array()) {
      $entities = $ids ? array_intersect_key($this->entityCache, array_flip($ids)) : $this->entityCache;
      return $this->applyConditions($entities, $conditions);
    }
    return array();
  }

  /**
   * Like cacheGet() but keyed by name.
   */
  protected function cacheGetByName($names, $conditions = array()) {
    if (!empty($this->entityCacheByName) && $names !== array() && $names) {
      // First get the entities by ids, then apply the conditions.
      // Generally, we make use of $this->entityCache, but if we are loading by
      // name, we have to use $this->entityCacheByName.
      $entities = array_intersect_key($this->entityCacheByName, array_flip($names));
      return $this->applyConditions($entities, $conditions);
    }
    return array();
  }

  protected function applyConditions($entities, $conditions = array()) {
    if ($conditions) {
      foreach ($entities as $key => $entity) {
        $entity_values = (array) $entity;
        // We cannot use array_diff_assoc() here because condition values can
        // also be arrays, e.g. '$conditions = array('status' => array(1, 2))'
        foreach ($conditions as $condition_key => $condition_value) {
          if (is_array($condition_value)) {
            if (!isset($entity_values[$condition_key]) || !in_array($entity_values[$condition_key], $condition_value)) {
              unset($entities[$key]);
            }
          }
          elseif (!isset($entity_values[$condition_key]) || $entity_values[$condition_key] != $condition_value) {
            unset($entities[$key]);
          }
        }
      }
    }
    return $entities;
  }

  /**
   * Overridden.
   * @see DrupalDefaultEntityController::cacheSet()
   */
  protected function cacheSet($entities) {
    $this->entityCache += $entities;
    // If we have a name key, also support static caching when loading by name.
    if ($this->nameKey != $this->idKey) {
      $this->entityCacheByName += entity_key_array_by_property($entities, $this->nameKey);
    }
  }

  /**
   * Overridden.
   * @see DrupalDefaultEntityController::attachLoad()
   *
   * Changed to call type-specific hook with the entities keyed by name if they
   * have one.
   */
  protected function attachLoad(&$queried_entities, $revision_id = FALSE) {
    // Attach fields.
    if ($this->entityInfo['fieldable']) {
      if ($revision_id) {
        field_attach_load_revision($this->entityType, $queried_entities);
      }
      else {
        field_attach_load($this->entityType, $queried_entities);
      }
    }

    // Call hook_entity_load().
    foreach (module_implements('entity_load') as $module) {
      $function = $module . '_entity_load';
      $function($queried_entities, $this->entityType);
    }
    // Call hook_TYPE_load(). The first argument for hook_TYPE_load() are
    // always the queried entities, followed by additional arguments set in
    // $this->hookLoadArguments.
    // For entities with a name key, pass the entities keyed by name to the
    // specific load hook.
    if ($this->nameKey != $this->idKey) {
      $entities_by_name = entity_key_array_by_property($queried_entities, $this->nameKey);
    }
    else {
      $entities_by_name = $queried_entities;
    }
    $args = array_merge(array($entities_by_name), $this->hookLoadArguments);
    foreach (module_implements($this->entityInfo['load hook']) as $module) {
      call_user_func_array($module . '_' . $this->entityInfo['load hook'], $args);
    }
  }

  public function resetCache(array $ids = NULL) {
    $this->cacheComplete = FALSE;
    if (isset($ids)) {
      foreach (array_intersect_key($this->entityCache, array_flip($ids)) as $id => $entity) {
        unset($this->entityCacheByName[$this->entityCache[$id]->{$this->nameKey}]);
        unset($this->entityCache[$id]);
      }
    }
    else {
      $this->entityCache = array();
      $this->entityCacheByName = array();
    }
  }

  /**
   * Overridden to care about reverted entities.
   */
  public function delete($ids, DatabaseTransaction $transaction = NULL) {
    $entities = $ids ? $this->load($ids) : FALSE;
    if ($entities) {
      parent::delete($ids, $transaction);

      foreach ($entities as $id => $entity) {
        if (entity_has_status($this->entityType, $entity, ENTITY_IN_CODE)) {
          entity_defaults_rebuild(array($this->entityType));
          break;
        }
      }
    }
  }

  /**
   * Overridden to care about reverted bundle entities and to skip Rules.
   */
  public function invoke($hook, $entity) {
    if ($hook == 'delete') {
      // To ease figuring out whether this is a revert, make sure that the
      // entity status is updated in case the providing module has been
      // disabled.
      if (entity_has_status($this->entityType, $entity, ENTITY_IN_CODE) && !module_exists($entity->{$this->moduleKey})) {
        $entity->{$this->statusKey} = ENTITY_CUSTOM;
      }
      $is_revert = entity_has_status($this->entityType, $entity, ENTITY_IN_CODE);
    }

    if (!empty($this->entityInfo['fieldable']) && function_exists($function = 'field_attach_' . $hook)) {
      $function($this->entityType, $entity);
    }

    if (isset($this->entityInfo['bundle of']) && $type = $this->entityInfo['bundle of']) {
      // Call field API bundle attachers for the entity we are a bundle of.
      if ($hook == 'insert') {
        field_attach_create_bundle($type, $entity->{$this->bundleKey});
      }
      elseif ($hook == 'delete' && !$is_revert) {
        field_attach_delete_bundle($type, $entity->{$this->bundleKey});
      }
      elseif ($hook == 'update' && $id = $entity->{$this->nameKey}) {
        if ($entity->original->{$this->bundleKey} != $entity->{$this->bundleKey}) {
          field_attach_rename_bundle($type, $entity->original->{$this->bundleKey}, $entity->{$this->bundleKey});
        }
      }
    }
    // Invoke the hook.
    module_invoke_all($this->entityType . '_' . $hook, $entity);
    // Invoke the respective entity level hook.
    if ($hook == 'presave' || $hook == 'insert' || $hook == 'update' || $hook == 'delete') {
      module_invoke_all('entity_' . $hook, $entity, $this->entityType);
    }
  }

  /**
   * Overridden to care exportables that are overridden.
   */
  public function save($entity, DatabaseTransaction $transaction = NULL) {
    // Preload $entity->original by name key if necessary.
    if (!empty($entity->{$this->nameKey}) && empty($entity->{$this->idKey}) && !isset($entity->original)) {
      $entity->original = entity_load_unchanged($this->entityType, $entity->{$this->nameKey});
    }
    // Update the status for entities getting overridden.
    if (entity_has_status($this->entityType, $entity, ENTITY_IN_CODE) && empty($entity->is_rebuild)) {
      $entity->{$this->statusKey} |= ENTITY_CUSTOM;
    }
    return parent::save($entity, $transaction);
  }

  /**
   * Overridden.
   */
  public function export($entity, $prefix = '') {
    $vars = get_object_vars($entity);
    unset($vars[$this->statusKey], $vars[$this->moduleKey], $vars['is_new']);
    if ($this->nameKey != $this->idKey) {
      unset($vars[$this->idKey]);
    }
    return entity_var_json_export($vars, $prefix);
  }

  /**
   * Implements OgEntityAPIControllerInterface.
   */
  public function view($entities, $view_mode = 'full', $langcode = NULL, $page = NULL) {
    $view = parent::view($entities, $view_mode, $langcode, $page);

    if ($this->nameKey != $this->idKey) {
      // Re-key the view array to be keyed by name.
      $return = array();
      foreach ($view[$this->entityType] as $id => $content) {
        $key = isset($content['#entity']->{$this->nameKey}) ? $content['#entity']->{$this->nameKey} : NULL;
        $return[$this->entityType][$key] = $content;
      }
      $view = $return;
    }
    return $view;
  }
}