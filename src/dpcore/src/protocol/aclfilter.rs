use crate::paint::{UserID, LayerID};
use super::message::*;

use std::collections::{HashMap, HashSet};
use std::convert::TryFrom;
use num_enum::IntoPrimitive;
use num_enum::TryFromPrimitive;

/// Bitfield for storing a set of users (IDs range from 0..255)
pub type UserBits = [u8; 8];

/// Feature access tiers
#[derive(Copy, Clone, Debug, PartialEq, PartialOrd, IntoPrimitive, TryFromPrimitive)]
#[repr(u8)]
pub enum Tier {
    Operator,
    Trusted,
    Authenticated,
    #[num_enum(default)]
    Guest,
}

#[repr(C)]
pub struct FeatureTiers {
    /// Use of the PutImage command (covers cut&paste, move with transform, etc.)
    pub put_image: Tier,

    /// Selection moving (without transformation)
    pub move_rect: Tier,

    /// Canvas resize
    pub resize: Tier,

    /// Canvas background changing
    pub background: Tier,

    /// Permission to edit any layer's properties and to reorganize them
    pub edit_layers: Tier,

    /// Permission to create and edit own layers
    pub own_layers: Tier,

    /// Permission to create new annotations
    pub create_annotation: Tier,

    /// Permission to use the laser pointer tool
    pub laser: Tier,

    /// Permission to use undo/redo
    pub undo: Tier
}

/// Set of general user related permission bits
#[repr(C)]
pub struct UserACLs {
    pub operators: UserBits,
    pub trusted: UserBits,
    pub authenticated: UserBits,
    pub locked: UserBits,
    pub all_locked: bool,
}

/// Layer specific permissions
#[repr(C)]
pub struct LayerACL {
    /// General layer-wide lock
    pub locked: bool,

    /// Layer general access tier
    pub tier: Tier,

    /// Exclusive access granted to these users
    /// Exclusive access overrides general access tier but not the lock.
    pub exclusive: UserBits,
}

/// Access Control List filtering for messages
pub struct AclFilter {
    users: UserACLs,
    layers: HashMap<LayerID, LayerACL>,
    locked_annotations: HashSet<LayerID>,
    feature_tier: FeatureTiers,
}

pub type AclChange = u32;

pub const ACLCHANGE_USERBITS: AclChange = 0x01;
pub const ACLCHANGE_LAYERS: AclChange = 0x02;
pub const ACLCHANGE_FEATURES: AclChange = 0x04;

impl UserACLs {
    fn new() -> Self {
        Self {
            operators: [0;8],
            trusted: [0;8],
            authenticated: [0;8],
            locked: [0;8],
            all_locked: false,
        }
    }

    fn is_op(&self, user: UserID) -> bool {
        user == 0 || is_userbit(&self.operators, user)
    }
}

impl AclFilter {
    /// Create a fresh ACL filter with default settings
    ///
    /// The local_op argument is set to true when running in local mode
    pub fn new() -> Self {
        Self {
            users: UserACLs::new(),
            layers: HashMap::new(),
            locked_annotations: HashSet::new(),
            feature_tier: FeatureTiers {
                put_image: Tier::Guest,
                move_rect: Tier::Guest,
                resize: Tier::Operator,
                background: Tier::Operator,
                edit_layers: Tier::Operator,
                own_layers: Tier::Guest,
                create_annotation: Tier::Guest,
                laser: Tier::Guest,
                undo: Tier::Guest,
            },
        }
    }

    pub fn users(&self) -> &UserACLs {
        &self.users
    }

    pub fn layers(&self) -> &HashMap<LayerID, LayerACL> {
        &self.layers
    }

    pub fn feature_tiers(&self) -> &FeatureTiers {
        &self.feature_tier
    }

    /// Reset the filter back to local operating mode
    pub fn reset(&mut self, local_user: UserID) {
        *self = AclFilter::new();

        set_userbit(&mut self.users.operators, local_user);
    }

    /// Evalute a message
    ///
    /// This will return true if the message passes the filter.
    /// Some messages will affect the state of the filter itself, in
    /// which case the affected state is returned also. When the
    /// returned AclChange is nonzero, the GUI layer can refresh
    /// itself to match the current state.
    pub fn filter_message(&mut self, msg: &Message) -> (bool, AclChange) {
        match msg {
            // We don't care about these
            Message::Control(_) => (true, 0),

            // No need to validate these but they may affect the filter's state
            Message::ServerMeta(m) => (true, self.handle_servermeta(m)),

            // These need to be validated and may affect the filter's state
            Message::ClientMeta(m) => self.handle_clientmeta(m),

            // These need to be validated but have no externally visible effect on the filter's state
            Message::Command(m) => (self.handle_command(m), 0),
        }
    }

    fn handle_servermeta(&mut self, message: &ServerMetaMessage) -> AclChange {
        use ServerMetaMessage::*;
        match message {
            Join(u, m) => {
                if (m.flags & JoinMessage::FLAGS_AUTH) != 0 {
                    set_userbit(&mut self.users.authenticated, *u);
                    return ACLCHANGE_USERBITS;
                }
            }
            Leave(u) => {
                unset_userbit(&mut self.users.operators, *u);
                unset_userbit(&mut self.users.trusted, *u);
                unset_userbit(&mut self.users.authenticated, *u);
                unset_userbit(&mut self.users.locked, *u);

                // TODO remove layer locks
                return ACLCHANGE_USERBITS;
            }
            SessionOwner(_, users) => {
                self.users.operators = vec_to_userbits(&users);
                return ACLCHANGE_USERBITS;
            }
            TrustedUsers(_, users) => {
                self.users.trusted = vec_to_userbits(&users);
                return ACLCHANGE_USERBITS;
            }
            // The other commands have no effect on the filter
            _ => ()
        };

        0
    }

    fn handle_clientmeta(&mut self, message: &ClientMetaMessage) -> (bool, AclChange) {
        use ClientMetaMessage::*;
        match message {
            // These only have effect in recordings
            Interval(_, _) => (true, 0),
            LaserTrail(u, _) => (self.users.tier(*u) <= self.feature_tier.laser, 0),
            MovePointer(_, _) => (true, 0),
            Marker(_, _) => (true, 0),
            UserACL(u, users) => {
                if self.users.is_op(*u) {
                    self.users.locked = vec_to_userbits(users);
                    (true, ACLCHANGE_USERBITS)
                } else {
                    (false, 0)
                }
            }
            LayerACL(u, m) => {
                let tier = self.users.tier(*u);
                if tier <= self.feature_tier.edit_layers || (tier <= self.feature_tier.own_layers && layer_creator(m.id) == *u) {
                    if m.flags == u8::from(Tier::Guest) && m.exclusive.is_empty() {
                        match self.layers.remove(&m.id) {
                            Some(_) => (true, ACLCHANGE_LAYERS),
                            None => (true, 0)
                        }
                    } else {
                        self.layers.insert(m.id, self::LayerACL {
                            locked: m.flags & 0x80 > 0,
                            tier: Tier::try_from(m.flags & 0x07).unwrap(),
                            exclusive: if m.exclusive.is_empty() {
                                [0xff;8]
                            } else {
                                vec_to_userbits(&m.exclusive)
                            }
                        });
                        (true, ACLCHANGE_LAYERS)
                    }
                } else {
                    (false, 0)
                }
            }
            FeatureAccessLevels(u, f) => {
                if self.users.is_op(*u) {
                    self.feature_tier = FeatureTiers {
                        put_image: Tier::try_from(f[0]).unwrap(),
                        move_rect: Tier::try_from(f[1]).unwrap(),
                        resize: Tier::try_from(f[2]).unwrap(),
                        background: Tier::try_from(f[3]).unwrap(),
                        edit_layers: Tier::try_from(f[4]).unwrap(),
                        own_layers: Tier::try_from(f[5]).unwrap(),
                        create_annotation: Tier::try_from(f[6]).unwrap(),
                        laser: Tier::try_from(f[7]).unwrap(),
                        undo: Tier::try_from(f[8]).unwrap(),
                    };
                    (true, ACLCHANGE_FEATURES)
                } else {
                    (false, 0)
                }
            }
            DefaultLayer(u, _) => (self.users.is_op(*u), 0),
            Filtered(_, _) => (false, 0),
        }
    }

    fn handle_command(&mut self, message: &CommandMessage) -> bool {
        // General and user specific locks apply to all command messages
        if self.users.all_locked || is_userbit(&self.users.locked, message.user()) {
            return false;
        }

        use CommandMessage::*;
        match message {
            UndoPoint(_) => true,
            CanvasResize(u, _) => self.users.tier(*u) <= self.feature_tier.resize,
            LayerCreate(u, m) => {
                if !self.users.is_op(*u) && layer_creator(m.id) != *u {
                    // enforce layer ID prefixing scheme for non-ops
                    return false;
                }
                let tier = self.users.tier(*u);
                tier <= self.feature_tier.edit_layers || tier <= self.feature_tier.own_layers
            }
            LayerAttributes(u, m) => self.check_layer_perms(*u, m.id),
            LayerRetitle(u, m) => self.check_layer_perms(*u, m.id),
            LayerOrder(u, _) => self.users.tier(*u) <= self.feature_tier.edit_layers,
            LayerDelete(u, m) => {
                let ok = self.check_layer_perms(*u, m.id);
                if ok {
                    self.layers.remove(&m.id);
                }
                ok
            }
            LayerVisibility(_, _) => true, // TODO
            PutImage(u, m) => self.users.tier(*u) <= self.feature_tier.put_image && !self.is_layer_locked(*u, m.layer),
            FillRect(u, m) => self.users.tier(*u) <= self.feature_tier.put_image && !self.is_layer_locked(*u, m.layer),
            PenUp(_) => true,
            AnnotationCreate(u, m) => self.users.tier(*u) <= self.feature_tier.create_annotation && (self.users.is_op(*u) || layer_creator(m.id) == *u),
            AnnotationReshape(u, m) => self.users.is_op(*u) || *u == layer_creator(m.id),
            AnnotationEdit(u, m) => {
                let ok = self.users.is_op(*u) || *u == layer_creator(m.id);
                if ok {
                    if m.flags & AnnotationEditMessage::FLAGS_PROTECT > 0 {
                        self.locked_annotations.insert(m.id);
                    } else {
                        self.locked_annotations.remove(&m.id);
                    }
                }
                ok
            }
            AnnotationDelete(u, id) => {
                let ok = self.users.is_op(*u) || *u == layer_creator(*id);
                if ok {
                    self.locked_annotations.remove(id);
                }
                ok
            }
            PutTile(u, _) => self.users.is_op(*u),
            CanvasBackground(u, _) => self.users.tier(*u) <= self.feature_tier.background,
            DrawDabsClassic(u, m) => !self.is_layer_locked(*u, m.layer),
            DrawDabsPixel(u, m) | DrawDabsPixelSquare(u, m) => !self.is_layer_locked(*u, m.layer),
            MoveRect(u, m) => self.users.tier(*u) <= self.feature_tier.move_rect && !self.is_layer_locked(*u, m.layer),
            Undo(u, _) => self.users.tier(*u) <= self.feature_tier.undo,
        }
    }

    fn check_layer_perms(&self, user: UserID, layer: LayerID) -> bool {
        let tier = self.users.tier(user);
        tier <= self.feature_tier.edit_layers || (user == layer_creator(layer) && tier <= self.feature_tier.own_layers)
    }

    fn is_layer_locked(&self, user: UserID, layer: LayerID) -> bool {
        self.layers.get(&layer).map_or(false, |l| l.locked || !is_userbit(&l.exclusive, user) || l.tier > self.users.tier(user))
    }
}

impl UserACLs {
    /// Get the highest access tier for this user based on the permission bits
    fn tier(&self, user: UserID) -> Tier {
        if user == 0 || is_userbit(&self.operators, user) {
            // ID 0 is reserved for server use
            Tier::Operator
        } else if is_userbit(&self.trusted, user) {
            Tier::Trusted
        } else if is_userbit(&self.authenticated, user) {
            Tier::Authenticated
        } else {
            Tier::Guest
        }
    }
}

fn set_userbit(bits: &mut UserBits, user: UserID) {
    bits[user as usize / 8] |= 1 << (user % 8);
}

fn unset_userbit(bits: &mut UserBits, user: UserID) {
    bits[user as usize / 8] &= !(1 << (user % 8));
}

fn vec_to_userbits(users: &[UserID]) -> UserBits {
    let mut bits: UserBits = [0;8];
    for u in users {
        set_userbit(&mut bits, *u);
    }

    bits
}

fn is_userbit(bits: &UserBits, user: UserID) -> bool {
    (bits[user as usize / 8] & (1 << (user % 8))) != 0
}

fn layer_creator(id: u16) -> UserID {
    (id >> 8) as UserID
}