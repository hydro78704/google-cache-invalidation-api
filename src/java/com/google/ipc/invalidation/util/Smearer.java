/*
 * Copyright 2011 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.google.ipc.invalidation.util;

import com.google.common.base.Preconditions;

import java.util.Random;

/**
 * An abstraction to "smear" values by a given percent. Useful for randomizing delays a little bit
 * so that (say) processes do not get synchronized on time inadvertently, e.g., a heartbeat task
 * that sends a message every few minutes is smeared so that all clients do not end up sending a
 * message at the same time. In particular, given a {@code delay}, returns a value that is randomly
 * distributed between [delay - smearPercent * delay, delay + smearPercent * delay]
 *
 */
public class Smearer {

  /** Default smearing to be done if the caller does not specify any. */
  
  public static final int DEFAULT_SMEAR_PERCENT = 20;

  private final Random random;

  /** The percentage (0, 1.0] for smearing the delay. */
  private final double smearFraction;

  /**
   * Creates a smearer with the given random number generator and default smear percent.
   */
  public Smearer(Random random) {
    this.random = random;
    this.smearFraction = DEFAULT_SMEAR_PERCENT / 100.0;
  }

  /**
   * Creates a smearer with the given random number generator. If {@code smearPercent} is 0, uses an
   * internal default for smearing.
   * <p>
   * REQUIRES: 0 < smearPercent <= 100
   */
  public Smearer(Random random, int smearPercent) {
    Preconditions.checkState((smearPercent > 0) && (smearPercent <= 100));
    this.random = random;
    this.smearFraction = smearPercent / 100.0;
  }

  /**
   * Given a {@code delay}, returns a value that is randomly distributed between
   * [delay - smearPercent * delay, delay + smearPercent * delay]
   */
  public int getSmearedDelay(int delay) {
    // Get a random number between -1 and 1 and then multiply that by the smear
    // fraction.
    double smearFactor = (2 * random.nextDouble() - 1.0) * smearFraction;
    return (int) Math.ceil(delay + delay * smearFactor);
  }
}
